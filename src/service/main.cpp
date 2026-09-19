// trackaccess-service - HTTP application service.
//
// Ownership model: this process owns all writes to the store. Clients never
// touch the store directly, so there is no shared-file-on-a-network-share
// arrangement to go wrong.
//
// Solving runs in a separate worker PROCESS (this binary spawns the trackaccess
// CLI) with CPU, memory and file-size rlimits applied in the child. A native
// solver fault therefore kills one job, not the service, and cannot corrupt the
// store: the worker writes only inside its own job directory.
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "core/instance.h"
#include "core/schedule.h"
#include "httplib.h"
#include "service/crypto.h"
#include "service/store.h"
#include "service/crypto.h"
#include "validator/validator.h"

namespace fs = std::filesystem;

namespace {

// The store's vocabulary (User, Role, Cap, Project, PlanVersion) is used
// throughout the handlers; core types stay explicitly ta::-qualified.
using ta::Cap;
using ta::InstanceRec;
using ta::PlanVersion;
using ta::Project;
using ta::Role;
using ta::ParseRole;
using ta::RoleHas;
using ta::Store;
using ta::ToString;
using ta::User;

// --------------------------------------------------------------------------
// Configuration
// --------------------------------------------------------------------------
struct Config {
  std::string host = "127.0.0.1";   // loopback by default; never all interfaces by accident
  int port = 8080;
  std::string root = "./var";
  std::string web = "./web";
  std::string worker;               // path to the trackaccess binary
  int session_idle_seconds = 1800;      // 30 minutes without activity
  int session_absolute_seconds = 28800; // 8 hours regardless of activity
  int max_concurrent_solves = 2;
  int max_queue = 32;
  double max_solve_seconds = 120.0;
  size_t max_upload_bytes = 32u * 1024 * 1024;
  int worker_memory_mb = 4096;
  std::string public_instance;   // optional bundled instance for the demo button
};

Config g_cfg;
Store g_store;

// Bounded, in-memory login throttle. Keyed by username so a slow attacker
// cannot lock every account out by hammering one; the queue cap bounds memory.
std::mutex g_throttle_mu;
std::unordered_map<std::string, std::pair<int, long long>> g_login_fails;

bool LoginThrottled(const std::string& who) {
  std::lock_guard<std::mutex> lk(g_throttle_mu);
  auto it = g_login_fails.find(who);
  if (it == g_login_fails.end()) return false;
  const long long now = static_cast<long long>(std::time(nullptr));
  if (now - it->second.second > 900) { g_login_fails.erase(it); return false; }
  return it->second.first >= 10;
}
void NoteLoginFailure(const std::string& who) {
  std::lock_guard<std::mutex> lk(g_throttle_mu);
  if (g_login_fails.size() > 10000) g_login_fails.clear();
  auto& e = g_login_fails[who];
  const long long now = static_cast<long long>(std::time(nullptr));
  if (now - e.second > 900) e = {0, now};
  e.first++;
  e.second = now;
}
void ClearLoginFailures(const std::string& who) {
  std::lock_guard<std::mutex> lk(g_throttle_mu);
  g_login_fails.erase(who);
}

std::string NowIso() {
  const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

std::string RandomId(size_t n = 16) {
  return ta::RandomToken((n + 1) / 2).substr(0, n);
}

// Ids come back from clients in paths. Only our own alphabet is ever accepted,
// which is what keeps a crafted id from escaping the store directory.
bool SafeId(const std::string& s) {
  if (s.empty() || s.size() > 40) return false;
  for (char c : s) if (!std::islower(static_cast<unsigned char>(c)) && !std::isdigit(static_cast<unsigned char>(c)))
    return false;
  return true;
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o.push_back(c);
    }
  }
  return o;
}
std::string Q(const std::string& s) { return "\"" + JsonEscape(s) + "\""; }

// --------------------------------------------------------------------------
// Jobs
// --------------------------------------------------------------------------
struct Job {
  std::string id;
  std::string instance_id;          // store row id, as text
  long long project_id = 0;
  long long instance_row = 0;
  long long actor_id = 0;
  bool fallback = false;
  bool strict_buffers = false;
  std::vector<long long> created_versions;
  std::string scenario;             // "A" | "B" | "C" | "all"
  std::string state = "queued";     // queued | running | done | failed | cancelled
  std::string created_at, started_at, finished_at;
  double seconds = 60;
  std::vector<std::string> progress;
  std::string log;
  std::string error;
  pid_t pid = 0;
  std::string instance_dir;
  std::string correlation_id;
  std::atomic<bool> cancel{false};
};

std::mutex g_mu;
std::condition_variable g_cv;
std::unordered_map<std::string, std::shared_ptr<Job>> g_jobs;
std::deque<std::string> g_queue;
int g_running = 0;
bool g_shutdown = false;

std::string JobDir(const std::string& id) { return g_cfg.root + "/jobs/" + id; }

std::string JobJson(const Job& j) {
  std::ostringstream os;
  os << "{" << Q("job_id") << ":" << Q(j.id)
     << "," << Q("instance_id") << ":" << Q(j.instance_id)
     << "," << Q("scenario") << ":" << Q(j.scenario)
     << "," << Q("state") << ":" << Q(j.state)
     << "," << Q("created_at") << ":" << Q(j.created_at)
     << "," << Q("started_at") << ":" << Q(j.started_at)
     << "," << Q("finished_at") << ":" << Q(j.finished_at)
     << "," << Q("error") << ":" << Q(j.error)
     << "," << Q("project_id") << ":" << j.project_id
     << "," << Q("version_ids") << ":[";
  for (size_t i = 0; i < j.created_versions.size(); ++i)
    os << (i ? "," : "") << j.created_versions[i];
  os << "]," << Q("progress") << ":[";
  for (size_t i = 0; i < j.progress.size(); ++i) os << (i ? "," : "") << Q(j.progress[i]);
  os << "]}";
  return os.str();
}

// Runs one solve as a child process. Returns the exit status, or -1 on spawn
// failure. Output is streamed back so the UI can show live progress.
int RunWorker(const std::shared_ptr<Job>& job) {
  const std::string in = job->instance_dir;
  const std::string out = JobDir(job->id);
  std::error_code ec;
  fs::create_directories(out, ec);

  int pipefd[2];
  if (::pipe(pipefd) != 0) return -1;

  const std::string secs = std::to_string(job->seconds);
  const std::string workers = "4";
  std::vector<std::string> argv_s = {g_cfg.worker, "solve", "--data", in, "--out", out,
                                     "--scenario", job->scenario, "--seconds", secs,
                                     "--workers", workers};
  if (job->fallback) argv_s.push_back("--fallback");
  if (job->strict_buffers) argv_s.push_back("--strict-buffers");
  std::vector<char*> argv;
  for (auto& s : argv_s) argv.push_back(const_cast<char*>(s.c_str()));
  argv.push_back(nullptr);

  const pid_t pid = ::fork();
  if (pid < 0) { ::close(pipefd[0]); ::close(pipefd[1]); return -1; }
  if (pid == 0) {
    // Child. Bound what a solve may consume so a pathological instance cannot
    // take the host down with it.
    ::close(pipefd[0]);
    ::dup2(pipefd[1], STDOUT_FILENO);
    ::dup2(pipefd[1], STDERR_FILENO);
    ::close(pipefd[1]);
    rlimit rl{};
    rl.rlim_cur = rl.rlim_max = static_cast<rlim_t>(g_cfg.worker_memory_mb) * 1024 * 1024;
    ::setrlimit(RLIMIT_AS, &rl);
    rl.rlim_cur = rl.rlim_max = static_cast<rlim_t>(job->seconds * 4 + 60);
    ::setrlimit(RLIMIT_CPU, &rl);
    rl.rlim_cur = rl.rlim_max = 256u * 1024 * 1024;      // no runaway output files
    ::setrlimit(RLIMIT_FSIZE, &rl);
    ::execv(g_cfg.worker.c_str(), argv.data());
    // This runs only after execv fails. stderr is already connected to the job
    // log, so preserve the operating-system reason instead of returning a
    // context-free exit code.
    int exec_errno = errno;
    const char prefix[] = "worker exec failed with errno ";
    ::write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    char digits[16];
    size_t count = 0;
    do {
      digits[count++] = static_cast<char>('0' + exec_errno % 10);
      exec_errno /= 10;
    } while (exec_errno > 0 && count < sizeof(digits));
    for (size_t i = 0; i < count / 2; ++i)
      std::swap(digits[i], digits[count - i - 1]);
    ::write(STDERR_FILENO, digits, count);
    const char newline = '\n';
    ::write(STDERR_FILENO, &newline, 1);
    ::_exit(127);
  }
  ::close(pipefd[1]);
  { std::lock_guard<std::mutex> lk(g_mu); job->pid = pid; }

  std::string buf;
  char chunk[4096];
  ssize_t n;
  while ((n = ::read(pipefd[0], chunk, sizeof chunk)) > 0) {
    buf.append(chunk, static_cast<size_t>(n));
    size_t nl;
    while ((nl = buf.find('\n')) != std::string::npos) {
      std::string line = buf.substr(0, nl);
      buf.erase(0, nl + 1);
      std::lock_guard<std::mutex> lk(g_mu);
      job->log += line + "\n";
      if (line.find("objective") != std::string::npos || line.find("status:") != std::string::npos ||
          line.find("===") != std::string::npos)
        job->progress.push_back(line);
      if (job->progress.size() > 400) job->progress.erase(job->progress.begin());
    }
    if (job->cancel.load()) { ::kill(pid, SIGTERM); }
  }
  ::close(pipefd[0]);
  int status = 0;
  ::waitpid(pid, &status, 0);
  { std::lock_guard<std::mutex> lk(g_mu); job->pid = 0; }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  return -2;   // killed by a signal: crash or cancellation
}

// Runs the worker synchronously for the short, interactive analyses (explain /
// repair) and returns its captured output. Same process isolation and rlimits as
// a solve; bounded by `seconds` so a request cannot occupy a thread indefinitely.
// Interactive analyses are worker processes too, and must obey the same
// concurrency bound as a solve. Without this, N simultaneous explain requests
// would fork N workers and walk straight past --max-solves.
class WorkerSlot {
 public:
  explicit WorkerSlot(std::chrono::milliseconds wait) {
    std::unique_lock<std::mutex> lk(g_mu);
    held_ = g_cv.wait_for(lk, wait, [] { return g_running < g_cfg.max_concurrent_solves; });
    if (held_) ++g_running;
  }
  ~WorkerSlot() {
    if (!held_) return;
    { std::lock_guard<std::mutex> lk(g_mu); --g_running; }
    g_cv.notify_all();
  }
  WorkerSlot(const WorkerSlot&) = delete;
  WorkerSlot& operator=(const WorkerSlot&) = delete;
  bool held() const { return held_; }

 private:
  bool held_ = false;
};

int RunWorkerSync(const std::vector<std::string>& argv_s, double seconds, std::string* output) {
  int pipefd[2];
  if (::pipe(pipefd) != 0) return -1;
  std::vector<char*> argv;
  for (auto& a : argv_s) argv.push_back(const_cast<char*>(a.c_str()));
  argv.push_back(nullptr);

  const pid_t pid = ::fork();
  if (pid < 0) { ::close(pipefd[0]); ::close(pipefd[1]); return -1; }
  if (pid == 0) {
    ::close(pipefd[0]);
    ::dup2(pipefd[1], STDOUT_FILENO);
    ::dup2(pipefd[1], STDERR_FILENO);
    ::close(pipefd[1]);
    rlimit rl{};
    rl.rlim_cur = rl.rlim_max = static_cast<rlim_t>(g_cfg.worker_memory_mb) * 1024 * 1024;
    ::setrlimit(RLIMIT_AS, &rl);
    rl.rlim_cur = rl.rlim_max = static_cast<rlim_t>(seconds * 8 + 60);
    ::setrlimit(RLIMIT_CPU, &rl);
    ::execv(g_cfg.worker.c_str(), argv.data());
    ::_exit(127);
  }
  ::close(pipefd[1]);
  char buf[4096];
  ssize_t n;
  while ((n = ::read(pipefd[0], buf, sizeof buf)) > 0) {
    output->append(buf, static_cast<size_t>(n));
    if (output->size() > 1u << 20) break;    // bound the reply
  }
  ::close(pipefd[0]);
  int status = 0;
  ::waitpid(pid, &status, 0);
  return WIFEXITED(status) ? WEXITSTATUS(status) : -2;
}

// Turns a finished job's output directories into immutable plan versions.
// Each version records the hash of the three competition files and the hash of
// the validation report, which is what an approval is later bound to.
void RecordPlanVersions(const std::shared_ptr<Job>& job) {
  const std::string base = JobDir(job->id);
  for (const char* sc : {"A", "B", "C"}) {
    const std::string dir = base + "/" + sc;
    std::string access, occupancy, results, validation;
    if (!ta::ReadFile(dir + "/SCHEDULE_ACCESS.csv", &access)) continue;
    ta::ReadFile(dir + "/SCHEDULE_OCCUPANCY.csv", &occupancy);
    ta::ReadFile(dir + "/RESULTS.csv", &results);
    ta::ReadFile(dir + "/VALIDATION.json", &validation);

    PlanVersion pv;
    pv.project_id = job->project_id;
    pv.instance_id = job->instance_row;
    pv.scenario = sc;
    pv.created_by = job->actor_id;
    pv.dir = dir;
    pv.strict_buffers = job->strict_buffers;
    // A fallback plan is identified by the marker the worker writes beside it,
    // not by what the caller asked for, so a plan can never lose the label.
    pv.is_fallback = fs::exists(dir + "/NOT_SUBMISSION_READY.txt");
    pv.content_hash = ta::Sha256Hex(access + occupancy + results);
    pv.validation_hash = ta::Sha256Hex(validation);
    pv.feasible = validation.find("\"feasible\": true") != std::string::npos;
    // Parse the one figure we index on; the report itself stays authoritative.
    // Scale to tenths BEFORE rounding, or 32.2 would be stored as 32.0.
    auto tenths_after = [&](const std::string& key) -> long long {
      const auto at = validation.find(key);
      if (at == std::string::npos) return 0;
      const auto colon = validation.find(':', at);
      if (colon == std::string::npos) return 0;
      try { return std::llround(10.0 * std::stod(validation.substr(colon + 1, 32))); }
      catch (...) { return 0; }
    };
    pv.violations = 0;
    { // count the entries in hard_violations
      const auto at = validation.find("\"hard_violations\"");
      if (at != std::string::npos) {
        const auto close = validation.find(']', at);
        const std::string seg = validation.substr(at, close == std::string::npos ? 0 : close - at);
        size_t pos = 0;
        while ((pos = seg.find("{\"rule\"", pos)) != std::string::npos) { ++pv.violations; ++pos; }
      }
    }
    pv.objective_tenths = tenths_after("\"objective_score\"");
    auto rec = g_store.InstanceById(job->instance_row);
    pv.input_hash = rec ? rec->input_hash : "";

    PlanVersion out;
    std::string err;
    if (g_store.AddPlanVersion(pv, &out, &err)) {
      { std::lock_guard<std::mutex> lk(g_mu); job->created_versions.push_back(out.id); }
      g_store.Audit(job->actor_id, "plan.create", "plan_version", std::to_string(out.id), "ok",
                    job->correlation_id,
                    std::string("scenario ") + sc + (pv.feasible ? " feasible" : " INFEASIBLE") +
                        (pv.is_fallback ? " (fallback, not submission-ready)" : ""));
    }
  }
}

void WorkerLoop() {
  for (;;) {
    std::string id;
    {
      std::unique_lock<std::mutex> lk(g_mu);
      g_cv.wait(lk, [] { return g_shutdown || (!g_queue.empty() && g_running < g_cfg.max_concurrent_solves); });
      if (g_shutdown) return;
      id = g_queue.front();
      g_queue.pop_front();
      ++g_running;
    }
    auto it = g_jobs.find(id);
    if (it == g_jobs.end()) { std::lock_guard<std::mutex> lk(g_mu); --g_running; continue; }
    auto job = it->second;
    { std::lock_guard<std::mutex> lk(g_mu); job->state = "running"; job->started_at = NowIso(); }

    const int rc = RunWorker(job);

    // Persist whatever the worker produced as immutable plan versions, before
    // the job is reported finished, so a version always exists by the time the
    // interface goes looking for one.
    if (!job->cancel.load()) RecordPlanVersions(job);

    {
      std::lock_guard<std::mutex> lk(g_mu);
      job->finished_at = NowIso();
      if (job->cancel.load()) { job->state = "cancelled"; job->error = "stopped by operator"; }
      else if (rc == 0) job->state = "done";
      else if (rc == 2) { job->state = "failed"; job->error = "no complete plan was found within the budget; this is not a proof that none exists"; }
      else if (rc == 3) { job->state = "failed"; job->error = "the produced plan did not pass the independent check"; }
      else if (rc == 1) { job->state = "failed"; job->error = "the instance was rejected; see the log"; }
      else if (rc == -2) { job->state = "failed"; job->error = "the solver process terminated abnormally; the service is unaffected"; }
      else if (rc == 127) {
        job->state = "failed";
        job->error = "solver startup failed; check the worker log and runtime libraries";
      }
      else { job->state = "failed"; job->error = "worker process failed with exit code " + std::to_string(rc) + "; see the log"; }
      --g_running;
    }
    g_cv.notify_all();
  }
}

// --------------------------------------------------------------------------
// Handlers
// --------------------------------------------------------------------------
void Deny(httplib::Response& res, int code, const std::string& msg) {
  res.status = code;
  res.set_content("{\"error\":" + Q(msg) + "}", "application/json");
}

std::string BearerOf(const httplib::Request& req) {
  auto it = req.headers.find("Authorization");
  if (it == req.headers.end()) return "";
  const std::string& v = it->second;
  if (v.rfind("Bearer ", 0) != 0) return "";
  return v.substr(7);
}

// Resolves the caller from their session. Every authorisation decision is made
// here, server-side, from the stored role - never from anything the client sent.
std::optional<User> CurrentUser(const httplib::Request& req) {
  return g_store.UserForSession(BearerOf(req));
}

std::string CorrelationId(const httplib::Request& req) {
  auto it = req.headers.find("X-Correlation-Id");
  if (it != req.headers.end() && it->second.size() <= 64) {
    std::string out;
    for (char c : it->second)
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') out.push_back(c);
    if (!out.empty()) return out;
  }
  return RandomId(12);
}

// Guard used at the top of every protected handler. Returns the caller, or
// writes the refusal and returns nullopt. Denial is the default: a handler that
// forgets to call this has no user to act as.
std::optional<User> Require(const httplib::Request& req, httplib::Response& res, Cap cap) {
  auto u = CurrentUser(req);
  if (!u) { Deny(res, 401, "sign in to continue"); return std::nullopt; }
  if (!RoleHas(u->role, cap)) {
    g_store.Audit(u->id, "authz.deny", "capability", std::to_string(static_cast<int>(cap)),
                  "denied", CorrelationId(req), "role " + std::string(ToString(u->role)));
    Deny(res, 403, "your role (" + std::string(ToString(u->role)) +
                   ") is not permitted to do this");
    return std::nullopt;
  }
  return u;
}

// Dataset protection: an uploaded instance belongs to its project, and a project
// is visible to its owner plus the roles that must review it. One judge's hidden
// instance is therefore not readable by another judge.
bool CanViewProject(const User& u, const Project& p) {
  if (p.owner_id == u.id) return true;
  return u.role == Role::kAdministrator || u.role == Role::kApprover;
}

std::optional<Project> RequireProject(const httplib::Request& req, httplib::Response& res,
                                      const User& u, long long id) {
  auto p = g_store.ProjectById(id);
  // A project the caller may not see is reported as absent rather than
  // forbidden, so the endpoint does not confirm that it exists.
  if (!p || !CanViewProject(u, *p)) { Deny(res, 404, "no such project"); return std::nullopt; }
  return p;
}

std::string UserJson(const User& u) {
  std::ostringstream os;
  os << "{" << Q("id") << ":" << u.id << "," << Q("username") << ":" << Q(u.username)
     << "," << Q("role") << ":" << Q(std::string(ToString(u.role)))
     << "," << Q("disabled") << ":" << (u.disabled ? "true" : "false")
     << "," << Q("created_at") << ":" << Q(u.created_at)
     << "," << Q("photo") << ":" << (g_store.HasUserPhoto(u.id) ? "true" : "false")
     << "," << Q("can") << ":{"
     << Q("create_project") << ":" << (RoleHas(u.role, Cap::kCreateProject) ? "true" : "false") << ","
     << Q("run_solve") << ":" << (RoleHas(u.role, Cap::kRunSolve) ? "true" : "false") << ","
     << Q("approve") << ":" << (RoleHas(u.role, Cap::kApprovePlan) ? "true" : "false") << ","
     << Q("manage_users") << ":" << (RoleHas(u.role, Cap::kManageUsers) ? "true" : "false") << ","
     << Q("view_audit") << ":" << (RoleHas(u.role, Cap::kViewAudit) ? "true" : "false")
     << "}}";
  return os.str();
}

std::string PlanVersionJson(const PlanVersion& p) {
  std::ostringstream os;
  auto owner = g_store.UserById(p.created_by);
  auto appr = p.approved_by ? g_store.UserById(p.approved_by) : std::nullopt;
  os << "{" << Q("id") << ":" << p.id
     << "," << Q("project_id") << ":" << p.project_id
     << "," << Q("instance_id") << ":" << p.instance_id
     << "," << Q("scenario") << ":" << Q(p.scenario)
     << "," << Q("version_no") << ":" << p.version_no
     << "," << Q("created_by") << ":" << Q(owner ? owner->username : "")
     << "," << Q("created_at") << ":" << Q(p.created_at)
     << "," << Q("feasible") << ":" << (p.feasible ? "true" : "false")
     << "," << Q("violations") << ":" << p.violations
     << "," << Q("objective") << ":" << (p.objective_tenths / 10) << "." << (p.objective_tenths % 10)
     << "," << Q("content_hash") << ":" << Q(p.content_hash)
     << "," << Q("validation_hash") << ":" << Q(p.validation_hash)
     << "," << Q("input_hash") << ":" << Q(p.input_hash)
     << "," << Q("is_fallback") << ":" << (p.is_fallback ? "true" : "false")
     << "," << Q("strict_buffers") << ":" << (p.strict_buffers ? "true" : "false")
     << "," << Q("status") << ":" << Q(p.status)
     << "," << Q("approved_by") << ":" << Q(appr ? appr->username : "")
     << "," << Q("approved_at") << ":" << Q(p.approved_at)
     // Approval is refused for these, server-side. Surfacing the reason lets the
     // interface explain rather than simply disable a control.
     << "," << Q("approvable") << ":"
     << ((p.feasible && !p.is_fallback && p.status == "draft") ? "true" : "false")
     << "," << Q("not_approvable_because") << ":"
     << Q(!p.feasible ? "it has hard violations"
          : p.is_fallback ? "it was produced in fallback mode and breaches its scenario policy"
          : p.status == "invalidated" ? "the input it was produced from is no longer current"
          : p.status == "approved" ? "it is already approved"
          : p.status == "superseded" ? "a newer version has been approved" : "")
     << "}";
  return os.str();
}

std::string SummariseInstance(const ta::Instance& inst) {
  std::ostringstream os;
  os << "{" << Q("activities") << ":" << inst.activities.size()
     << "," << Q("contracts") << ":" << inst.contracts.size()
     << "," << Q("locations") << ":" << inst.locations.size()
     << "," << Q("horizon_weeks") << ":" << inst.horizon_weeks
     << "," << Q("horizon_start") << ":" << Q(inst.horizon_start.ToIso())
     << "," << Q("input_hash") << ":" << Q(inst.input_hash)
     << "," << Q("exclusive_pairs") << ":" << inst.exclusive_pairs.size()
     << "," << Q("total_accesses") << ":";
  int total = 0;
  for (const auto& a : inst.activities) total += a.total_accesses;
  os << total << "}";
  return os.str();
}


// Full model for the interface: network, contracts, activities with their
// expanded spans and closure zones. Canonical identifiers throughout.
std::string InstanceDetailJson(const ta::Instance& inst) {
  std::ostringstream os;
  os << "{" << Q("summary") << ":" << SummariseInstance(inst) << "," << Q("locations") << ":[";
  for (size_t i = 0; i < inst.locations.size(); ++i) {
    const auto& L = inst.locations[i];
    os << (i ? "," : "") << "{" << Q("id") << ":" << Q(L.id)
       << "," << Q("kind") << ":" << Q(L.kind == ta::LocationKind::kTunnelSector ? "sector" : "platform")
       << "," << Q("line") << ":" << Q(L.line)
       << "," << Q("bound") << ":" << Q(L.bound == ta::Bound::kEB ? "EB" : "WB")
       << "," << Q("supply") << ":" << L.supply_capacity
       << "," << Q("chain") << ":" << L.chain_index << "}";
  }
  os << "]," << Q("contracts") << ":[";
  for (size_t i = 0; i < inst.contracts.size(); ++i) {
    const auto& c = inst.contracts[i];
    os << (i ? "," : "") << "{" << Q("number") << ":" << Q(c.number)
       << "," << Q("description") << ":" << Q(c.description)
       << "," << Q("nature") << ":" << Q(std::string(ta::ToString(c.nature)))
       << "," << Q("access_type") << ":" << Q(std::string(ta::ToString(c.access_type)))
       << "," << Q("priority") << ":" << c.priority
       << "," << Q("planned") << ":" << Q(c.planned_completion_date.ToIso())
       << "," << Q("contractual") << ":" << Q(c.contract_completion_date.ToIso())
       << "," << Q("planned_week") << ":" << c.planned_completion_week
       << "," << Q("workfronts") << ":" << c.number_of_workfronts
       << "," << Q("max_access_per_week") << ":" << c.max_access_per_week << "}";
  }
  os << "]," << Q("activities") << ":[";
  for (size_t i = 0; i < inst.activities.size(); ++i) {
    const auto& a = inst.activities[i];
    os << (i ? "," : "") << "{" << Q("id") << ":" << Q(a.id)
       << "," << Q("contract") << ":" << Q(inst.contracts[a.contract].number)
       << "," << Q("total_accesses") << ":" << a.total_accesses
       << "," << Q("priority") << ":" << a.activity_priority
       << "," << Q("earliest_week") << ":" << a.earliest_week
       << "," << Q("planned_start") << ":" << Q(a.planned_start_date.ToIso())
       << "," << Q("from") << ":" << Q(a.start_location_id)
       << "," << Q("to") << ":" << Q(a.end_location_id)
       << "," << Q("predecessor") << ":"
       << (a.predecessor == ta::kNoIndex ? "null" : Q(inst.activities[a.predecessor].id))
       << "," << Q("occupied") << ":[";
    for (size_t k = 0; k < a.occupied.size(); ++k)
      os << (k ? "," : "") << Q(inst.locations[a.occupied[k]].id);
    os << "]," << Q("closure") << ":[";
    for (size_t k = 0; k < a.buffer_zone.size(); ++k)
      os << (k ? "," : "") << Q(inst.locations[a.buffer_zone[k]].id);
    os << "]}";
  }
  os << "]}";
  return os.str();
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  auto arg = [&](const std::string& k, const std::string& d) {
    for (size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == k) return args[i + 1];
    return d;
  };
  g_cfg.host = arg("--host", g_cfg.host);
  g_cfg.port = std::stoi(arg("--port", "8080"));
  g_cfg.root = arg("--root", g_cfg.root);
  g_cfg.web = arg("--web", g_cfg.web);
  g_cfg.worker = arg("--worker", fs::path(argv[0]).parent_path().string() + "/trackaccess");
  g_cfg.max_solve_seconds = std::stod(arg("--max-seconds", "120"));
  g_cfg.max_concurrent_solves = std::stoi(arg("--max-solves", "2"));
  g_cfg.public_instance = arg("--public-instance", "");
  g_cfg.session_idle_seconds = std::stoi(arg("--session-idle", "1800"));
  g_cfg.session_absolute_seconds = std::stoi(arg("--session-max", "28800"));

  if (!fs::exists(g_cfg.worker)) {
    std::cerr << "worker binary not found at " << g_cfg.worker
              << " (pass --worker /path/to/trackaccess)\n";
    return 1;
  }
  std::error_code ec;
  fs::create_directories(g_cfg.root + "/instances", ec);
  fs::create_directories(g_cfg.root + "/jobs", ec);
  fs::create_directories(g_cfg.root + "/repairs", ec);

  std::string serr;
  if (!g_store.Open(g_cfg.root + "/trackaccess.sqlite", &serr)) {
    std::cerr << "cannot open the store: " << serr << "\n";
    return 1;
  }
  g_store.PurgeExpiredSessions();

  // Optional first-run account, so a fresh deployment is usable without a
  // separate admin tool. Ignored once any account exists.
  const std::string boot = arg("--bootstrap-admin", "");
  if (!boot.empty()) {
    const auto colon = boot.find(':');
    if (colon == std::string::npos) {
      std::cerr << "--bootstrap-admin expects username:password\n";
      return 1;
    }
    if (g_store.UserCount() == 0) {
      User u;
      std::string e;
      if (!g_store.CreateUser(boot.substr(0, colon), boot.substr(colon + 1),
                              Role::kAdministrator, &u, &e)) {
        std::cerr << "could not create the initial administrator: " << e << "\n";
        return 1;
      }
      std::cout << "created initial administrator: " << u.username << "\n";
    } else {
      std::cout << "accounts already exist; --bootstrap-admin ignored\n";
    }
  }

  httplib::Server srv;
  srv.set_payload_max_length(g_cfg.max_upload_bytes);

  // A browser page from any other origin must not be able to drive this service.
  // No CORS headers are emitted at all, so cross-origin reads are refused by the
  // browser; same-origin UI is unaffected.
  srv.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
    if (req.path.rfind("/api/", 0) == 0) {
      res.set_header("Cache-Control", "no-store, private");
      res.set_header("Pragma", "no-cache");
      res.set_header("Vary", "Authorization");
    }
    res.set_header("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
    res.set_header("Referrer-Policy", "no-referrer");
    res.set_header("Content-Security-Policy",
                   "default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; "
                   "script-src 'self'; connect-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'; object-src 'none'");
  });

  // Reject browser-origin writes from other sites, including login/bootstrap.
  // No forwarded headers are trusted here; a proxy must preserve the Host.
  srv.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) {
    using Result = httplib::Server::HandlerResponse;
    if (req.path.rfind("/api/", 0) != 0) return Result::Unhandled;
    if (req.method != "GET" && req.method != "HEAD" && req.method != "OPTIONS") {
      const auto origin = req.get_header_value("Origin");
      const auto host = req.get_header_value("Host");
      if (req.get_header_value("Sec-Fetch-Site") == "cross-site" ||
          (!origin.empty() && origin != "http://" + host && origin != "https://" + host)) {
        Deny(res, 403, "cross-origin changes are not permitted");
        return Result::Handled;
      }
    }
    // Single-service, bounded per-IP budget. A distributed edge limiter is
    // still required for a multi-instance deployment. Do not trust X-Forwarded-For.
    if (req.method == "POST") {
      struct Bucket { std::chrono::steady_clock::time_point start; int count; };
      static std::mutex rate_mutex;
      static std::unordered_map<std::string, Bucket> buckets;
      const auto now = std::chrono::steady_clock::now();
      const bool auth = req.path == "/api/v1/auth/login" || req.path == "/api/v1/bootstrap";
      const auto key = req.remote_addr + (auth ? ":auth" : ":write");
      std::lock_guard<std::mutex> lock(rate_mutex);
      for (auto it = buckets.begin(); it != buckets.end();) {
        if (now - it->second.start >= std::chrono::minutes(1)) it = buckets.erase(it);
        else ++it;
      }
      if (buckets.size() >= 10000 && !buckets.count(key)) {
        res.set_header("Retry-After", "60"); Deny(res, 429, "service is busy; try again later"); return Result::Handled;
      }
      auto [it, inserted] = buckets.try_emplace(key, Bucket{now, 0});
      if (++it->second.count > (auth ? 60 : 180)) {
        res.set_header("Retry-After", "60"); Deny(res, 429, "request limit reached; wait one minute"); return Result::Handled;
      }
    }
    return Result::Unhandled;
  });
  srv.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
    Deny(res, 500, "the service could not complete the request");
  });

  // ---------------------------------------------------------------- health
  srv.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::ostringstream os;
    os << "{" << Q("status") << ":" << Q("ok")
       << "," << Q("queued") << ":" << g_queue.size()
       << "," << Q("running") << ":" << g_running
       << "," << Q("needs_bootstrap") << ":" << (g_store.UserCount() == 0 ? "true" : "false")
       << "," << Q("time") << ":" << Q(NowIso()) << "}";
    res.set_content(os.str(), "application/json");
  });

  // ---------------------------------------------------------------- auth
  // First-run only: creates the initial administrator. Refused once any account
  // exists, so it cannot be used to mint a second one later.
  srv.Post("/api/v1/bootstrap", [](const httplib::Request& req, httplib::Response& res) {
    static std::mutex bootstrap_mutex;
    std::lock_guard<std::mutex> bootstrap_lock(bootstrap_mutex);
    if (g_store.UserCount() != 0) return Deny(res, 409, "this deployment is already set up");
    const std::string user = req.get_param_value("username");
    const std::string pass = req.get_param_value("password");
    std::string err;
    User u;
    if (!g_store.CreateUser(user, pass, Role::kAdministrator, &u, &err)) return Deny(res, 400, err);
    g_store.Audit(u.id, "bootstrap", "user", std::to_string(u.id), "ok", CorrelationId(req),
                  "initial administrator created");
    res.set_content("{" + Q("user") + ":" + UserJson(u) + "}", "application/json");
  });

  srv.Post("/api/v1/auth/login", [](const httplib::Request& req, httplib::Response& res) {
    const std::string user = req.get_param_value("username");
    const std::string pass = req.get_param_value("password");
    if (user.empty() || pass.empty()) return Deny(res, 400, "username and password are required");
    if (user.size() > 64 || pass.size() > 1024) return Deny(res, 400, "credentials exceed the size limit");
    if (LoginThrottled(user)) {
      g_store.Audit(0, "auth.login", "user", user, "throttled", CorrelationId(req), "");
      return Deny(res, 429, "too many failed attempts; wait a few minutes and try again");
    }
    auto u = g_store.Authenticate(user, pass);
    if (!u) {
      NoteLoginFailure(user);
      g_store.Audit(0, "auth.login", "user", user, "denied", CorrelationId(req), "");
      // One message for every failure mode, so the response does not reveal
      // whether the account exists or is merely disabled.
      return Deny(res, 401, "those credentials were not accepted");
    }
    ClearLoginFailures(user);
    const std::string token =
        g_store.CreateSession(u->id, g_cfg.session_idle_seconds, g_cfg.session_absolute_seconds);
    if (token.empty()) return Deny(res, 500, "could not start a session");
    g_store.Audit(u->id, "auth.login", "user", std::to_string(u->id), "ok", CorrelationId(req), "");
    std::ostringstream os;
    os << "{" << Q("token") << ":" << Q(token) << "," << Q("user") << ":" << UserJson(*u)
       << "," << Q("idle_seconds") << ":" << g_cfg.session_idle_seconds
       << "," << Q("absolute_seconds") << ":" << g_cfg.session_absolute_seconds << "}";
    res.set_content(os.str(), "application/json");
  });

  srv.Post("/api/v1/auth/logout", [](const httplib::Request& req, httplib::Response& res) {
    auto u = CurrentUser(req);
    g_store.RevokeSession(BearerOf(req));
    if (u) g_store.Audit(u->id, "auth.logout", "user", std::to_string(u->id), "ok",
                         CorrelationId(req), "");
    res.set_content("{\"ok\":true}", "application/json");
  });

  srv.Get("/api/v1/auth/me", [](const httplib::Request& req, httplib::Response& res) {
    auto u = CurrentUser(req);
    if (!u) return Deny(res, 401, "sign in to continue");
    res.set_content("{" + Q("user") + ":" + UserJson(*u) + "}", "application/json");
  });

  // ---------------------------------------------------------------- users
  srv.Get("/api/v1/users", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kManageUsers);
    if (!me) return;
    std::ostringstream os;
    os << "{" << Q("users") << ":[";
    const auto all = g_store.ListUsers();
    for (size_t i = 0; i < all.size(); ++i) os << (i ? "," : "") << UserJson(all[i]);
    os << "]}";
    res.set_content(os.str(), "application/json");
  });

  srv.Post("/api/v1/users", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kManageUsers);
    if (!me) return;
    const auto role = ParseRole(req.get_param_value("role"));
    if (!role) return Deny(res, 400, "role must be viewer, planner, approver or administrator");
    User u;
    std::string err;
    if (!g_store.CreateUser(req.get_param_value("username"), req.get_param_value("password"),
                            *role, &u, &err)) {
      g_store.Audit(me->id, "user.create", "user", req.get_param_value("username"), "failed",
                    CorrelationId(req), err);
      return Deny(res, 400, err);
    }
    g_store.Audit(me->id, "user.create", "user", std::to_string(u.id), "ok", CorrelationId(req),
                  "role " + std::string(ToString(*role)));
    res.set_content("{" + Q("user") + ":" + UserJson(u) + "}", "application/json");
  });

  srv.Post(R"(/api/v1/users/(\d+)/role)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kManageUsers);
    if (!me) return;
    const long long id = std::stoll(req.matches[1]);
    const auto role = ParseRole(req.get_param_value("role"));
    if (!role) return Deny(res, 400, "unknown role");
    std::string err;
    if (!g_store.SetUserRole(id, *role, &err)) return Deny(res, 400, err);
    g_store.Audit(me->id, "user.role", "user", std::to_string(id), "ok", CorrelationId(req),
                  "set to " + std::string(ToString(*role)));
    res.set_content("{\"ok\":true}", "application/json");
  });

  srv.Post(R"(/api/v1/users/(\d+)/disable)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kManageUsers);
    if (!me) return;
    const long long id = std::stoll(req.matches[1]);
    if (id == me->id) return Deny(res, 400, "you cannot disable your own account");
    const bool off = req.get_param_value("disabled") != "0";
    std::string err;
    if (!g_store.SetUserDisabled(id, off, &err)) return Deny(res, 400, err);
    g_store.Audit(me->id, off ? "user.disable" : "user.enable", "user", std::to_string(id), "ok",
                  CorrelationId(req), "");
    res.set_content("{\"ok\":true}", "application/json");
  });

  // ---------------------------------------------------------------- projects
  srv.Get("/api/v1/projects", [](const httplib::Request& req, httplib::Response& res) {
    auto me = CurrentUser(req);
    if (!me) return Deny(res, 401, "sign in to continue");
    long long before = 0;
    int limit = 50;
    try {
      if (req.has_param("before")) {
        const auto value = req.get_param_value("before");
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return Deny(res,400,"invalid cursor");
        before = std::stoll(value);
      }
      if (req.has_param("limit")) {
        const auto value = req.get_param_value("limit");
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return Deny(res,400,"invalid limit");
        limit = std::stoi(value);
        if (limit < 1 || limit > 100) return Deny(res,400,"limit must be 1..100");
      }
    } catch (...) { return Deny(res,400,"invalid pagination"); }
    auto rows = g_store.ListVisibleProjects(*me, before, limit + 1);
    const bool more = rows.size() > static_cast<size_t>(limit);
    if (more) rows.resize(limit);
    std::ostringstream os;
    os << "{\"projects\":[";
    bool first = true;
    for (const auto& p : rows) {
      os << (first ? "" : ",") << "{" << Q("id") << ":" << p.id
         << "," << Q("name") << ":" << Q(p.name)
         << "," << Q("owner") << ":" << Q(p.owner_name)
         << "," << Q("revision") << ":" << p.revision
         << "," << Q("created_at") << ":" << Q(p.created_at) << "}";
      first = false;
    }
    os << "],\"next_cursor\":" << (more ? std::to_string(rows.back().id) : "null") << "}";
    res.set_content(os.str(), "application/json");
  });
  srv.Get(R"(/api/v1/projects/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
    auto me = CurrentUser(req);
    if (!me) return Deny(res,401,"sign in to continue");
    auto p = RequireProject(req,res,*me,std::stoll(req.matches[1]));
    if (!p) return;
    res.set_content("{\"id\":" + std::to_string(p->id) + ",\"name\":" + Q(p->name) +
      ",\"revision\":" + std::to_string(p->revision) + "}", "application/json");
  });

  srv.Post("/api/v1/projects", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kCreateProject);
    if (!me) return;
    Project p;
    std::string err;
    if (!g_store.CreateProject(req.get_param_value("name"), me->id, &p, &err))
      return Deny(res, 400, err);
    g_store.Audit(me->id, "project.create", "project", std::to_string(p.id), "ok",
                  CorrelationId(req), p.name);
    std::ostringstream os;
    os << "{" << Q("id") << ":" << p.id << "," << Q("name") << ":" << Q(p.name)
       << "," << Q("revision") << ":" << p.revision << "}";
    res.set_content(os.str(), "application/json");
  });

  // Upload the eight instance CSVs into a project. Replacing the input
  // invalidates any plan previously approved against the old one.
  srv.Post(R"(/api/v1/projects/(\d+)/instances)",
           [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kUploadInstance);
    if (!me) return;
    auto proj = RequireProject(req, res, *me, std::stoll(req.matches[1]));
    if (!proj) return;

    const std::string key = RandomId();
    const std::string dir = g_cfg.root + "/instances/" + key;
    std::error_code ec2;
    fs::create_directories(dir, ec2);
    int written = 0;
    for (const char* const name : ta::kInstanceFiles) {
      auto it = req.files.find(name);
      if (it == req.files.end()) continue;
      std::ofstream f(dir + "/" + name, std::ios::binary);
      f << it->second.content;
      ++written;
    }
    if (written != 8) {
      fs::remove_all(dir, ec2);
      std::ostringstream os;
      os << "{" << Q("error") << ":" << Q("expected all eight instance files")
         << "," << Q("received") << ":" << written << "," << Q("expected_names") << ":[";
      for (int i = 0; i < 8; ++i) os << (i ? "," : "") << Q(ta::kInstanceFiles[i]);
      os << "]}";
      res.status = 400;
      res.set_content(os.str(), "application/json");
      return;
    }
    ta::Instance inst;
    std::vector<ta::InputError> errors;
    if (!ta::LoadInstance(dir, &inst, &errors)) {
      fs::remove_all(dir, ec2);
      std::ostringstream os;
      os << "{" << Q("accepted") << ":false," << Q("errors") << ":[";
      for (size_t i = 0; i < errors.size() && i < 200; ++i)
        os << (i ? "," : "") << "{" << Q("file") << ":" << Q(errors[i].file)
           << "," << Q("row") << ":" << errors[i].row
           << "," << Q("field") << ":" << Q(errors[i].field)
           << "," << Q("message") << ":" << Q(errors[i].message) << "}";
      os << "]," << Q("error_count") << ":" << errors.size() << "}";
      res.status = 422;
      res.set_content(os.str(), "application/json");
      g_store.Audit(me->id, "instance.upload", "project", std::to_string(proj->id), "rejected",
                    CorrelationId(req), std::to_string(errors.size()) + " input problems");
      return;
    }
    InstanceRec rec;
    rec.project_id = proj->id;
    rec.dir = dir;
    rec.input_hash = inst.input_hash;
    rec.uploaded_by = me->id;
    rec.label = req.get_param_value("label");
    std::string err;
    if (!g_store.AddInstance(rec, &rec, &err)) return Deny(res, 500, err);
    const int invalidated =
        g_store.InvalidateApprovalsForChangedInput(proj->id, inst.input_hash, me->id);
    g_store.Audit(me->id, "instance.upload", "instance", std::to_string(rec.id), "ok",
                  CorrelationId(req),
                  "input_hash " + inst.input_hash.substr(0, 12) +
                      (invalidated ? "; invalidated " + std::to_string(invalidated) + " earlier plan(s)" : ""));
    std::ostringstream os;
    os << "{" << Q("instance_id") << ":" << rec.id << "," << Q("accepted") << ":true,"
       << Q("invalidated_plans") << ":" << invalidated
       << "," << Q("summary") << ":" << SummariseInstance(inst) << "}";
    res.set_content(os.str(), "application/json");
  });

  // Convenience for reviewers: copies the bundled public instance into a project.
  srv.Post(R"(/api/v1/projects/(\d+)/instances/demo)",
           [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kUploadInstance);
    if (!me) return;
    auto proj = RequireProject(req, res, *me, std::stoll(req.matches[1]));
    if (!proj) return;
    if (g_cfg.public_instance.empty() || !fs::exists(g_cfg.public_instance))
      return Deny(res, 404, "no public instance is bundled with this deployment");
    const std::string dir = g_cfg.root + "/instances/" + RandomId();
    std::error_code ec2;
    fs::create_directories(dir, ec2);
    for (const char* const name : ta::kInstanceFiles)
      fs::copy_file(g_cfg.public_instance + "/" + name, dir + "/" + name,
                    fs::copy_options::overwrite_existing, ec2);
    ta::Instance inst;
    std::vector<ta::InputError> errors;
    if (!ta::LoadInstance(dir, &inst, &errors)) {
      fs::remove_all(dir, ec2);
      return Deny(res, 500, "the bundled public instance failed to load");
    }
    InstanceRec rec;
    rec.project_id = proj->id;
    rec.dir = dir;
    rec.input_hash = inst.input_hash;
    rec.uploaded_by = me->id;
    rec.label = "bundled public instance";
    std::string err;
    if (!g_store.AddInstance(rec, &rec, &err)) return Deny(res, 500, err);
    const int invalidated =
        g_store.InvalidateApprovalsForChangedInput(proj->id, inst.input_hash, me->id);
    g_store.Audit(me->id, "instance.upload", "instance", std::to_string(rec.id), "ok",
                  CorrelationId(req), "bundled public instance");
    std::ostringstream os;
    os << "{" << Q("instance_id") << ":" << rec.id << "," << Q("accepted") << ":true,"
       << Q("bundled") << ":true," << Q("invalidated_plans") << ":" << invalidated
       << "," << Q("summary") << ":" << SummariseInstance(inst) << "}";
    res.set_content(os.str(), "application/json");
  });

  srv.Get(R"(/api/v1/projects/(\d+)/instances)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = CurrentUser(req);
    if (!me) return Deny(res, 401, "sign in to continue");
    auto proj = RequireProject(req, res, *me, std::stoll(req.matches[1]));
    if (!proj) return;
    std::ostringstream os;
    os << "{" << Q("instances") << ":[";
    const auto all = g_store.ListInstances(proj->id);
    for (size_t i = 0; i < all.size(); ++i) {
      auto up = g_store.UserById(all[i].uploaded_by);
      os << (i ? "," : "") << "{" << Q("id") << ":" << all[i].id
         << "," << Q("input_hash") << ":" << Q(all[i].input_hash)
         << "," << Q("uploaded_by") << ":" << Q(up ? up->username : "")
         << "," << Q("created_at") << ":" << Q(all[i].created_at)
         << "," << Q("label") << ":" << Q(all[i].label) << "}";
    }
    os << "]}";
    res.set_content(os.str(), "application/json");
  });

  srv.Get(R"(/api/v1/instances/(\d+)/detail)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = CurrentUser(req);
    if (!me) return Deny(res, 401, "sign in to continue");
    auto rec = g_store.InstanceById(std::stoll(req.matches[1]));
    if (!rec) return Deny(res, 404, "unknown instance");
    auto proj = RequireProject(req, res, *me, rec->project_id);
    if (!proj) return;
    ta::Instance inst;
    std::vector<ta::InputError> errs;
    if (!ta::LoadInstance(rec->dir, &inst, &errs)) return Deny(res, 500, "instance no longer loads");
    res.set_content(InstanceDetailJson(inst), "application/json");
  });

  // ---------------------------------------------------------------- jobs
  srv.Post(R"(/api/v1/projects/(\d+)/jobs)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kRunSolve);
    if (!me) return;
    auto proj = RequireProject(req, res, *me, std::stoll(req.matches[1]));
    if (!proj) return;

    // Optimistic concurrency: the caller states the project revision they were
    // looking at. A stale one is refused with the current state rather than
    // quietly racing another planner.
    const std::string expect = req.get_param_value("expected_revision");
    if (!expect.empty()) {
      long long want = -1;
      try { want = std::stoll(expect); } catch (...) {}
      if (want != proj->revision) {
        std::ostringstream os;
        os << "{" << Q("error") << ":"
           << Q("this project changed while you were working on it") << ","
           << Q("your_revision") << ":" << want << "," << Q("current_revision") << ":"
           << proj->revision << "}";
        res.status = 409;
        res.set_content(os.str(), "application/json");
        return;
      }
    }
    auto rec = g_store.InstanceById(std::stoll(req.get_param_value("instance_id").empty()
                                                   ? "0" : req.get_param_value("instance_id")));
    if (!rec || rec->project_id != proj->id) return Deny(res, 404, "unknown instance");

    std::string scenario = req.get_param_value("scenario");
    if (scenario.empty()) scenario = "all";
    if (scenario != "all" && !ta::ParseScenario(scenario))
      return Deny(res, 400, "scenario must be A, B, C or all");
    double seconds = 60;
    if (!req.get_param_value("seconds").empty()) {
      try { seconds = std::stod(req.get_param_value("seconds")); } catch (...) {}
    }
    seconds = std::clamp(seconds, 1.0, g_cfg.max_solve_seconds);

    auto job = std::make_shared<Job>();
    job->id = RandomId();
    job->instance_id = std::to_string(rec->id);
    job->project_id = proj->id;
    job->instance_row = rec->id;
    job->actor_id = me->id;
    job->fallback = req.get_param_value("fallback") == "1";
    job->strict_buffers = req.get_param_value("strict_buffers") == "1";
    job->scenario = scenario;
    job->seconds = seconds;
    job->created_at = NowIso();
    job->instance_dir = rec->dir;
    job->correlation_id = CorrelationId(req);
    {
      std::lock_guard<std::mutex> lk(g_mu);
      if (static_cast<int>(g_queue.size()) >= g_cfg.max_queue)
        return Deny(res, 429, "the solve queue is full; try again shortly");
      g_jobs[job->id] = job;
      g_queue.push_back(job->id);
    }
    std::string err;
    g_store.BumpProjectRevision(proj->id, proj->revision, &err);
    g_store.Audit(me->id, "job.create", "project", std::to_string(proj->id), "ok",
                  job->correlation_id, "scenario " + scenario);
    g_cv.notify_all();
    res.set_content(JobJson(*job), "application/json");
  });

  srv.Get(R"(/api/v1/jobs/([a-z0-9]+))", [](const httplib::Request& req, httplib::Response& res) {
    auto me = CurrentUser(req);
    if (!me) return Deny(res, 401, "sign in to continue");
    std::shared_ptr<Job> job;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      auto it = g_jobs.find(req.matches[1]);
      if (it == g_jobs.end()) return Deny(res, 404, "unknown job");
      job = it->second;
    }
    auto proj = RequireProject(req, res, *me, job->project_id);
    if (!proj) return;
    std::lock_guard<std::mutex> lk(g_mu);
    res.set_content(JobJson(*job), "application/json");
  });

  srv.Post(R"(/api/v1/jobs/([a-z0-9]+)/cancel)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kRunSolve);
    if (!me) return;
    std::shared_ptr<Job> job;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      auto it = g_jobs.find(req.matches[1]);
      if (it == g_jobs.end()) return Deny(res, 404, "unknown job");
      job = it->second;
    }
    auto proj = RequireProject(req, res, *me, job->project_id);
    if (!proj) return;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      job->cancel.store(true);
      if (job->pid > 0) ::kill(job->pid, SIGTERM);
      if (job->state == "queued") { job->state = "cancelled"; job->finished_at = NowIso(); }
    }
    // Logged against the project, not the job: job ids are in-memory and random,
    // so a "job"-typed row could never be attributed to a project after a
    // restart and would be dropped from the project's audit view. The job id is
    // kept in the detail field.
    g_store.Audit(me->id, "job.cancel", "project", std::to_string(job->project_id), "ok",
                  CorrelationId(req), "job " + job->id);
    std::lock_guard<std::mutex> lk(g_mu);
    res.set_content(JobJson(*job), "application/json");
  });

  srv.Get(R"(/api/v1/jobs/([a-z0-9]+)/log)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = CurrentUser(req);
    if (!me) return Deny(res, 401, "sign in to continue");
    std::shared_ptr<Job> job;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      auto it = g_jobs.find(req.matches[1]);
      if (it == g_jobs.end()) return Deny(res, 404, "unknown job");
      job = it->second;
    }
    auto proj = RequireProject(req, res, *me, job->project_id);
    if (!proj) return;
    std::lock_guard<std::mutex> lk(g_mu);
    res.set_content(job->log, "text/plain");
  });

  // ---------------------------------------------------------------- versions
  srv.Get(R"(/api/v1/projects/(\d+)/versions)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = CurrentUser(req);
    if (!me) return Deny(res, 401, "sign in to continue");
    auto proj = RequireProject(req, res, *me, std::stoll(req.matches[1]));
    if (!proj) return;
    std::ostringstream os;
    os << "{" << Q("revision") << ":" << proj->revision << "," << Q("versions") << ":[";
    const auto all = g_store.ListPlanVersions(proj->id);
    for (size_t i = 0; i < all.size(); ++i) os << (i ? "," : "") << PlanVersionJson(all[i]);
    os << "]}";
    res.set_content(os.str(), "application/json");
  });

  srv.Get(R"(/api/v1/versions/(\d+)/validation)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = CurrentUser(req);
    if (!me) return Deny(res, 401, "sign in to continue");
    auto pv = g_store.PlanVersionById(std::stoll(req.matches[1]));
    if (!pv) return Deny(res, 404, "unknown plan version");
    auto proj = RequireProject(req, res, *me, pv->project_id);
    if (!proj) return;
    std::string body;
    if (!ta::ReadFile(pv->dir + "/VALIDATION.json", &body))
      return Deny(res, 404, "no validation report for that version");
    res.set_content(body, "application/json");
  });

  srv.Get(R"(/api/v1/versions/(\d+)/files/([A-Z_]+\.csv))",
          [](const httplib::Request& req, httplib::Response& res) {
    auto me = CurrentUser(req);
    if (!me) return Deny(res, 401, "sign in to continue");
    const std::string name = req.matches[2];
    if (name != "SCHEDULE_ACCESS.csv" && name != "SCHEDULE_OCCUPANCY.csv" && name != "RESULTS.csv")
      return Deny(res, 404, "not a competition output file");
    auto pv = g_store.PlanVersionById(std::stoll(req.matches[1]));
    if (!pv) return Deny(res, 404, "unknown plan version");
    auto proj = RequireProject(req, res, *me, pv->project_id);
    if (!proj) return;
    std::string body;
    if (!ta::ReadFile(pv->dir + "/" + name, &body)) return Deny(res, 404, "file not produced");
    res.set_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
    res.set_content(body, "text/csv");
  });

  // Approval. The store refuses an infeasible or fallback plan regardless of who
  // asks, and binds the approval to the exact content and validation the
  // approver was shown.
  srv.Post(R"(/api/v1/versions/(\d+)/approve)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kApprovePlan);
    if (!me) return;
    const long long id = std::stoll(req.matches[1]);
    auto pv = g_store.PlanVersionById(id);
    if (!pv) return Deny(res, 404, "unknown plan version");
    auto proj = RequireProject(req, res, *me, pv->project_id);
    if (!proj) return;
    std::string err;
    if (!g_store.ApprovePlan(id, me->id, req.get_param_value("content_hash"),
                             req.get_param_value("validation_hash"), &err)) {
      g_store.Audit(me->id, "plan.approve", "plan_version", std::to_string(id), "refused",
                    CorrelationId(req), err);
      return Deny(res, 409, err);
    }
    g_store.Audit(me->id, "plan.approve", "plan_version", std::to_string(id), "ok",
                  CorrelationId(req),
                  "scenario " + pv->scenario + " v" + std::to_string(pv->version_no) +
                      " content " + pv->content_hash.substr(0, 12));
    auto now = g_store.PlanVersionById(id);
    res.set_content("{" + Q("version") + ":" + PlanVersionJson(*now) + "}", "application/json");
  });

  srv.Post(R"(/api/v1/versions/(\d+)/revoke)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kApprovePlan);
    if (!me) return;
    const long long id = std::stoll(req.matches[1]);
    auto pv = g_store.PlanVersionById(id);
    if (!pv) return Deny(res, 404, "unknown plan version");
    auto proj = RequireProject(req, res, *me, pv->project_id);
    if (!proj) return;
    std::string err;
    const std::string reason = req.get_param_value("reason");
    if (!g_store.RevokeApproval(id, me->id, reason, &err)) return Deny(res, 409, err);
    g_store.Audit(me->id, "plan.revoke", "plan_version", std::to_string(id), "ok",
                  CorrelationId(req), reason);
    auto now = g_store.PlanVersionById(id);
    res.set_content("{" + Q("version") + ":" + PlanVersionJson(*now) + "}", "application/json");
  });

  // ---------------------------------------------------------------- audit
  srv.Get(R"(/api/v1/projects/(\d+)/audit)", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kViewAudit);
    if (!me) return;
    auto proj = RequireProject(req, res, *me, std::stoll(req.matches[1]));
    if (!proj) return;
    std::ostringstream os;
    os << "{" << Q("events") << ":[";
    const auto ev = g_store.ListProjectAudit(proj->id, 300);
    bool first = true;
    for (const auto& e : ev) {
      os << (first ? "" : ",") << "{" << Q("ts") << ":" << Q(e.ts)
         << "," << Q("actor") << ":" << Q(e.actor_name)
         << "," << Q("action") << ":" << Q(e.action)
         << "," << Q("object") << ":" << Q(e.object_type + " " + e.object_id)
         << "," << Q("result") << ":" << Q(e.result)
         << "," << Q("correlation_id") << ":" << Q(e.correlation_id)
         << "," << Q("detail") << ":" << Q(e.detail) << "}";
      first = false;
    }
    os << "]}";
    res.set_content(os.str(), "application/json");
  });

  // Installation-wide audit, including account events that belong to no project.
  // Separate endpoint, separate authorisation: kViewAudit alone is not enough,
  // because an approver holds it and must still only see their projects.
  srv.Get("/api/v1/audit", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kViewAudit);
    if (!me) return;
    if (me->role != Role::kAdministrator)
      return Deny(res, 403, "installation audit needs the administrator role");
    std::ostringstream os;
    os << "{" << Q("events") << ":[";
    const auto ev = g_store.ListAudit(300, "", "");
    bool first = true;
    for (const auto& e : ev) {
      os << (first ? "" : ",") << "{" << Q("ts") << ":" << Q(e.ts)
         << "," << Q("actor") << ":" << Q(e.actor_name)
         << "," << Q("action") << ":" << Q(e.action)
         << "," << Q("object") << ":" << Q(e.object_type + " " + e.object_id)
         << "," << Q("result") << ":" << Q(e.result)
         << "," << Q("correlation_id") << ":" << Q(e.correlation_id)
         << "," << Q("detail") << ":" << Q(e.detail) << "}";
      first = false;
    }
    os << "]}";
    res.set_content(os.str(), "application/json");
  });

  // ------------------------------------------------- coordinator assignments
  // Communication metadata. The solver never reads it, and it is kept out of
  // the eight input CSVs and the three export CSVs entirely.

  srv.Get(R"(/api/v1/projects/(\d+)/assignments)",
          [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kViewProject);
    if (!me) return;
    auto proj = RequireProject(req, res, *me, std::stoll(req.matches[1]));
    if (!proj) return;
    long long iid = 0;
    try { iid = std::stoll(req.get_param_value("instance_id")); } catch (...) {}
    if (iid <= 0) return Deny(res, 400, "instance_id is required");
    // The instance must be this project's, or a caller could read another
    // project's assignments through a project they can see.
    auto rec = g_store.InstanceById(iid);
    if (!rec || rec->project_id != proj->id) return Deny(res, 404, "unknown instance");

    std::ostringstream os;
    os << "{" << Q("assignments") << ":[";
    bool first = true;
    for (const auto& a : g_store.ListAssignments(proj->id, iid)) {
      os << (first ? "" : ",") << "{" << Q("activity_id") << ":" << Q(a.activity_id)
         << "," << Q("coordinator_id") << ":" << a.coordinator_id
         << "," << Q("coordinator") << ":" << Q(a.coordinator_name)
         << "," << Q("coordinator_photo") << ":"
         << (a.coordinator_id && g_store.HasUserPhoto(a.coordinator_id) ? "true" : "false")
         << "," << Q("assigned_at") << ":" << Q(a.assigned_at) << "}";
      first = false;
    }
    os << "]}";
    res.set_content(os.str(), "application/json");
  });

  srv.Post(R"(/api/v1/projects/(\d+)/assignments)",
           [](const httplib::Request& req, httplib::Response& res) {
    // Assigning is a planning action, so it needs the capability that plans the
    // work - a viewer may read an assignment but never set one.
    auto me = Require(req, res, Cap::kUploadInstance);
    if (!me) return;
    auto proj = RequireProject(req, res, *me, std::stoll(req.matches[1]));
    if (!proj) return;

    long long iid = 0, coord = 0;
    try { iid = std::stoll(req.get_param_value("instance_id")); } catch (...) {}
    const std::string activity = req.get_param_value("activity_id");
    const std::string coord_raw = req.get_param_value("coordinator_id");
    // An empty coordinator clears the assignment; the activity then reads as
    // "Unassigned" rather than quietly inheriting whoever uploaded the files.
    if (!coord_raw.empty()) { try { coord = std::stoll(coord_raw); } catch (...) { coord = -1; } }
    if (iid <= 0) return Deny(res, 400, "instance_id is required");
    if (activity.empty()) return Deny(res, 400, "activity_id is required");
    if (coord < 0) return Deny(res, 400, "coordinator_id must be a number, or empty to clear");

    auto rec = g_store.InstanceById(iid);
    if (!rec || rec->project_id != proj->id) return Deny(res, 404, "unknown instance");

    std::string err;
    if (!g_store.SetAssignment(proj->id, iid, activity, coord, me->id, &err))
      return Deny(res, 400, err);

    // Recorded against the project, so it appears in that project's trail and
    // nowhere else.
    g_store.Audit(me->id, coord ? "activity.assign" : "activity.unassign", "project",
                  std::to_string(proj->id), "ok", CorrelationId(req),
                  "instance " + std::to_string(iid) + " activity " + activity);
    res.set_content("{" + Q("ok") + ":true}", "application/json");
  });

  // ------------------------------------------------------------ profile photo

  srv.Get(R"(/api/v1/users/(\d+)/photo)", [](const httplib::Request& req, httplib::Response& res) {
    // Any signed-in user may see a colleague's avatar; it is shown beside their
    // name throughout the workspace.
    auto me = Require(req, res, Cap::kViewProject);
    if (!me) return;
    std::string type, bytes;
    if (!g_store.GetUserPhoto(std::stoll(req.matches[1]), &type, &bytes))
      return Deny(res, 404, "no photo");
    // nosniff is already set globally; the type here is one we verified on the
    // way in, never one the client asserted.
    res.set_header("Cache-Control", "private, max-age=60");
    res.set_content(bytes, type.c_str());
  });

  srv.Post("/api/v1/profile/photo", [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kViewProject);
    if (!me) return;

    auto it = req.files.find("photo");
    if (it == req.files.end()) {
      // No file part means "remove my photo".
      g_store.ClearUserPhoto(me->id);
      g_store.Audit(me->id, "profile.photo.clear", "user", std::to_string(me->id), "ok",
                    CorrelationId(req), "");
      return res.set_content("{" + Q("ok") + ":true," + Q("photo") + ":false}", "application/json");
    }
    const std::string& body = it->second.content;

    // Size cap first: everything below reads the buffer.
    if (body.size() > 512u * 1024u)
      return Deny(res, 413, "the photo must be 512 kB or smaller");
    if (body.size() < 16) return Deny(res, 400, "that file is not an image");

    // The media type is decided by the bytes, never by the client's header or
    // the filename. SVG is refused outright: it is a document that can carry
    // script, not a raster image.
    auto starts = [&](const char* sig, size_t n) { return body.compare(0, n, sig, n) == 0; };
    std::string type;
    int w = 0, h = 0;
    if (starts("\x89PNG\r\n\x1a\n", 8)) {
      type = "image/png";
      // IHDR width/height are big-endian at bytes 16..23 of a valid PNG.
      auto be32 = [&](size_t o) {
        return (static_cast<unsigned char>(body[o]) << 24) |
               (static_cast<unsigned char>(body[o + 1]) << 16) |
               (static_cast<unsigned char>(body[o + 2]) << 8) |
               (static_cast<unsigned char>(body[o + 3]));
      };
      if (body.size() < 24 || body.compare(12, 4, "IHDR") != 0)
        return Deny(res, 400, "that PNG file is not readable");
      w = be32(16); h = be32(20);
    } else if (starts("\xff\xd8\xff", 3)) {
      type = "image/jpeg";
      // Walk the segment chain to the frame header for the real dimensions.
      size_t i = 2;
      while (i + 9 < body.size()) {
        if (static_cast<unsigned char>(body[i]) != 0xFF) break;
        const unsigned char marker = static_cast<unsigned char>(body[i + 1]);
        const size_t len = (static_cast<unsigned char>(body[i + 2]) << 8) |
                            static_cast<unsigned char>(body[i + 3]);
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
          h = (static_cast<unsigned char>(body[i + 5]) << 8) | static_cast<unsigned char>(body[i + 6]);
          w = (static_cast<unsigned char>(body[i + 7]) << 8) | static_cast<unsigned char>(body[i + 8]);
          break;
        }
        if (len < 2) break;
        i += 2 + len;
      }
      if (w == 0 || h == 0) return Deny(res, 400, "that JPEG file is not readable");
    } else {
      return Deny(res, 415, "use a PNG or JPEG image");
    }

    if (w <= 0 || h <= 0 || w > 2048 || h > 2048)
      return Deny(res, 400, "the photo must be 2048 by 2048 pixels or smaller");

    std::string err;
    if (!g_store.SetUserPhoto(me->id, type, body, &err)) return Deny(res, 500, err);
    g_store.Audit(me->id, "profile.photo.set", "user", std::to_string(me->id), "ok",
                  CorrelationId(req), type);
    res.set_content("{" + Q("ok") + ":true," + Q("photo") + ":true}", "application/json");
  });

  // ------------------------------------------------- explain / repair
  srv.Post(R"(/api/v1/instances/(\d+)/explain)",
           [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kRunSolve);
    if (!me) return;
    auto rec = g_store.InstanceById(std::stoll(req.matches[1]));
    if (!rec) return Deny(res, 404, "unknown instance");
    auto proj = RequireProject(req, res, *me, rec->project_id);
    if (!proj) return;
    const std::string act = req.get_param_value("activity");
    const std::string week = req.get_param_value("week");
    std::string scen = req.get_param_value("scenario");
    if (scen.empty()) scen = "A";
    auto plain = [](const std::string& v, size_t max) {
      if (v.empty() || v.size() > max) return false;
      for (char c : v) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
      return true;
    };
    if (!plain(act, 40) || !plain(week, 4) || !ta::ParseScenario(scen))
      return Deny(res, 400, "malformed activity, week or scenario");
    double seconds = 15;
    if (!req.get_param_value("seconds").empty())
      try { seconds = std::clamp(std::stod(req.get_param_value("seconds")), 1.0, 60.0); } catch (...) {}
    WorkerSlot slot(std::chrono::seconds(5));
    if (!slot.held())
      return Deny(res, 429, "the solver is busy; try again in a moment");
    std::string out;
    const int rc = RunWorkerSync({g_cfg.worker, "explain", "--data", rec->dir,
                                  "--activity", act, "--week", week, "--scenario", scen,
                                  "--seconds", std::to_string(seconds)}, seconds, &out);
    g_store.Audit(me->id, "plan.explain", "instance", std::to_string(rec->id), "ok",
                  CorrelationId(req), act + " week " + week);
    std::ostringstream os;
    os << "{" << Q("exit") << ":" << rc << "," << Q("output") << ":" << Q(out) << "}";
    res.set_content(os.str(), "application/json");
  });

  srv.Post(R"(/api/v1/instances/(\d+)/repair)",
           [](const httplib::Request& req, httplib::Response& res) {
    auto me = Require(req, res, Cap::kRunSolve);
    if (!me) return;
    auto rec = g_store.InstanceById(std::stoll(req.matches[1]));
    if (!rec) return Deny(res, 404, "unknown instance");
    auto proj = RequireProject(req, res, *me, rec->project_id);
    if (!proj) return;
    std::string scen = req.get_param_value("scenario");
    if (scen.empty()) scen = "A";
    if (!ta::ParseScenario(scen)) return Deny(res, 400, "scenario must be A, B or C");
    double seconds = 45;
    if (!req.get_param_value("seconds").empty())
      try { seconds = std::clamp(std::stod(req.get_param_value("seconds")), 1.0, 90.0); } catch (...) {}
    std::vector<std::string> specs;
    for (const auto& [k, v] : req.params) {
      if (k != "supply") continue;
      if (v.size() > 80) return Deny(res, 400, "supply specification too long");
      for (char c : v)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != ':' && c != '_' &&
            c != '@' && c != '=' && c != '-')
          return Deny(res, 400, "malformed supply specification");
      specs.push_back(v);
      if (specs.size() > 40) return Deny(res, 400, "too many supply overrides");
    }
    if (specs.empty()) return Deny(res, 400, "at least one supply override is required");
    const std::string outdir = g_cfg.root + "/repairs/" + RandomId();
    std::error_code ec2;
    fs::create_directories(outdir, ec2);
    std::vector<std::string> argv = {g_cfg.worker, "repair", "--data", rec->dir,
                                     "--out", outdir, "--scenario", scen,
                                     "--seconds", std::to_string(seconds)};
    for (const auto& sp : specs) { argv.push_back("--supply"); argv.push_back(sp); }
    WorkerSlot slot(std::chrono::seconds(5));
    if (!slot.held())
      return Deny(res, 429, "the solver is busy; try again in a moment");
    std::string out;
    const int rc = RunWorkerSync(argv, seconds, &out);
    g_store.Audit(me->id, "plan.repair", "instance", std::to_string(rec->id), rc == 0 ? "ok" : "failed",
                  CorrelationId(req), std::to_string(specs.size()) + " supply overrides");
    std::ostringstream os;
    os << "{" << Q("exit") << ":" << rc << "," << Q("output") << ":" << Q(out) << "}";
    res.set_content(os.str(), "application/json");
  });

  srv.set_mount_point("/", g_cfg.web);

  // Single-page fallback.
  //
  // The React client owns its routes, so opening or refreshing /workspace sends
  // a request the service has no file for. That navigation must get the app
  // shell. Three things it must NOT do:
  //   - answer an unknown /api path with HTML. An API path stays an API path;
  //     returning a page there turns a 404 into a JSON parse error at the caller.
  //   - answer a missing asset (anything with a dot in the last segment) with
  //     HTML, which would mask a broken script tag as a blank page.
  //   - overwrite a response a handler already wrote. Only a genuinely unmatched
  //     request has an empty body at this point.
  srv.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
    if (res.status != 404 || !res.body.empty()) return;
    if (req.path.rfind("/api/", 0) == 0) {
      res.set_content("{\"error\":\"no such endpoint\"}", "application/json");
      return;
    }
    if (req.method != "GET") return;
    const auto slash = req.path.find_last_of('/');
    const auto last = slash == std::string::npos ? req.path : req.path.substr(slash + 1);
    if (last.find('.') != std::string::npos) return;   // a missing file, not a route
    std::ifstream f(g_cfg.web + "/index.html", std::ios::binary);
    if (!f) return;
    std::ostringstream ss;
    ss << f.rdbuf();
    res.status = 200;
    res.set_content(ss.str(), "text/html");
  });

  std::vector<std::thread> pool;
  for (int i = 0; i < g_cfg.max_concurrent_solves; ++i) pool.emplace_back(WorkerLoop);

  std::cout << "trackaccess-service listening on http://" << g_cfg.host << ":" << g_cfg.port << "\n"
            << "  store       " << fs::absolute(g_cfg.root).string() << "\n"
            << "  web root    " << fs::absolute(g_cfg.web).string() << "\n"
            << "  worker      " << g_cfg.worker << "\n"
            << "  accounts    " << g_store.UserCount() << "\n"
            << "  sessions    idle " << g_cfg.session_idle_seconds << "s, absolute "
            << g_cfg.session_absolute_seconds << "s\n";
  if (g_store.UserCount() == 0)
    std::cout << "  NOTE: no accounts yet. Open the interface and create the first\n"
                 "        administrator, or restart with --bootstrap-admin user:password.\n";
  // Flush explicitly: when the service is started under nohup or a supervisor,
  // stdout is a pipe and the banner would otherwise sit in the buffer until the
  // process exits - exactly when an operator no longer needs it.
  std::cout << std::flush;
  if (g_cfg.host != "127.0.0.1" && g_cfg.host != "localhost")
    std::cout << "  NOTE: bound to a non-loopback address; put TLS in front of this service.\n";

  if (!srv.listen(g_cfg.host, g_cfg.port)) {
    std::cerr << "failed to bind " << g_cfg.host << ":" << g_cfg.port << "\n";
    return 1;
  }
  { std::lock_guard<std::mutex> lk(g_mu); g_shutdown = true; }
  g_cv.notify_all();
  for (auto& t : pool) t.join();
  return 0;
}
