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
#include "validator/validator.h"

namespace fs = std::filesystem;

namespace {

// --------------------------------------------------------------------------
// Configuration
// --------------------------------------------------------------------------
struct Config {
  std::string host = "127.0.0.1";   // loopback by default; never all interfaces by accident
  int port = 8080;
  std::string root = "./var";
  std::string web = "./web";
  std::string worker;               // path to the trackaccess binary
  std::string token;                // bearer token; empty only with --auth none
  bool require_auth = true;
  int max_concurrent_solves = 2;
  int max_queue = 32;
  double max_solve_seconds = 120.0;
  size_t max_upload_bytes = 32u * 1024 * 1024;
  int worker_memory_mb = 4096;
  std::string public_instance;   // optional bundled instance for the demo button
};

Config g_cfg;

std::string NowIso() {
  const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

std::string RandomId(size_t n = 16) {
  static std::mt19937_64 rng(std::random_device{}());
  static std::mutex mu;
  static const char* kAlpha = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::lock_guard<std::mutex> lk(mu);
  std::string s;
  for (size_t i = 0; i < n; ++i) s.push_back(kAlpha[rng() % 36]);
  return s;
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
  std::string instance_id;
  std::string scenario;             // "A" | "B" | "C" | "all"
  std::string state = "queued";     // queued | running | done | failed | cancelled
  std::string created_at, started_at, finished_at;
  double seconds = 60;
  std::vector<std::string> progress;
  std::string log;
  std::string error;
  pid_t pid = 0;
  std::atomic<bool> cancel{false};
};

std::mutex g_mu;
std::condition_variable g_cv;
std::unordered_map<std::string, std::shared_ptr<Job>> g_jobs;
std::deque<std::string> g_queue;
int g_running = 0;
bool g_shutdown = false;

std::string JobDir(const std::string& id) { return g_cfg.root + "/jobs/" + id; }
std::string InstanceDir(const std::string& id) { return g_cfg.root + "/instances/" + id; }

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
     << "," << Q("progress") << ":[";
  for (size_t i = 0; i < j.progress.size(); ++i) os << (i ? "," : "") << Q(j.progress[i]);
  os << "]}";
  return os.str();
}

// Runs one solve as a child process. Returns the exit status, or -1 on spawn
// failure. Output is streamed back so the UI can show live progress.
int RunWorker(const std::shared_ptr<Job>& job) {
  const std::string in = InstanceDir(job->instance_id);
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

    {
      std::lock_guard<std::mutex> lk(g_mu);
      job->finished_at = NowIso();
      if (job->cancel.load()) { job->state = "cancelled"; job->error = "stopped by operator"; }
      else if (rc == 0) job->state = "done";
      else if (rc == 2) { job->state = "failed"; job->error = "no complete plan was found within the budget; this is not a proof that none exists"; }
      else if (rc == 3) { job->state = "failed"; job->error = "the produced plan did not pass the independent check"; }
      else if (rc == 1) { job->state = "failed"; job->error = "the instance was rejected; see the log"; }
      else if (rc == -2) { job->state = "failed"; job->error = "the solver process terminated abnormally; the service is unaffected"; }
      else { job->state = "failed"; job->error = "worker process could not be started"; }
      --g_running;
    }
    g_cv.notify_all();
  }
}

// --------------------------------------------------------------------------
// Handlers
// --------------------------------------------------------------------------
bool Authorised(const httplib::Request& req) {
  if (!g_cfg.require_auth) return true;
  auto it = req.headers.find("Authorization");
  if (it == req.headers.end()) return false;
  const std::string expect = "Bearer " + g_cfg.token;
  // Length-independent compare avoids leaking the token through timing.
  if (it->second.size() != expect.size()) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < expect.size(); ++i) diff |= static_cast<unsigned>(it->second[i] ^ expect[i]);
  return diff == 0;
}

void Deny(httplib::Response& res, int code, const std::string& msg) {
  res.status = code;
  res.set_content("{\"error\":" + Q(msg) + "}", "application/json");
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
  const std::string auth = arg("--auth", "token");
  g_cfg.require_auth = (auth != "none");
  g_cfg.token = arg("--token", "");
  if (g_cfg.require_auth && g_cfg.token.empty()) g_cfg.token = RandomId(32);

  if (!fs::exists(g_cfg.worker)) {
    std::cerr << "worker binary not found at " << g_cfg.worker
              << " (pass --worker /path/to/trackaccess)\n";
    return 1;
  }
  std::error_code ec;
  fs::create_directories(g_cfg.root + "/instances", ec);
  fs::create_directories(g_cfg.root + "/jobs", ec);

  httplib::Server srv;
  srv.set_payload_max_length(g_cfg.max_upload_bytes);

  // A browser page from any other origin must not be able to drive this service.
  // No CORS headers are emitted at all, so cross-origin reads are refused by the
  // browser; same-origin UI is unaffected.
  srv.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
    res.set_header("X-Content-Type-Options", "nosniff");
    res.set_header("X-Frame-Options", "DENY");
    res.set_header("Referrer-Policy", "no-referrer");
    res.set_header("Content-Security-Policy",
                   "default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
                   "script-src 'self'; connect-src 'self'; base-uri 'none'; form-action 'self'");
  });

  srv.Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
    std::lock_guard<std::mutex> lk(g_mu);
    std::ostringstream os;
    os << "{" << Q("status") << ":" << Q("ok")
       << "," << Q("queued") << ":" << g_queue.size()
       << "," << Q("running") << ":" << g_running
       << "," << Q("auth_required") << ":" << (g_cfg.require_auth ? "true" : "false")
       << "," << Q("time") << ":" << Q(NowIso()) << "}";
    res.set_content(os.str(), "application/json");
  });

  // Upload the eight instance CSVs. Rejected uploads report every problem with
  // file, row and field so a planner can fix the source data.
  srv.Post("/api/v1/instances", [](const httplib::Request& req, httplib::Response& res) {
    if (!Authorised(req)) return Deny(res, 401, "authentication required");
    const std::string id = RandomId();
    const std::string dir = InstanceDir(id);
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
      std::ostringstream os;
      os << "{" << Q("instance_id") << ":null," << Q("accepted") << ":false,"
         << Q("errors") << ":[";
      for (size_t i = 0; i < errors.size() && i < 200; ++i)
        os << (i ? "," : "") << "{" << Q("file") << ":" << Q(errors[i].file)
           << "," << Q("row") << ":" << errors[i].row
           << "," << Q("field") << ":" << Q(errors[i].field)
           << "," << Q("message") << ":" << Q(errors[i].message) << "}";
      os << "]," << Q("error_count") << ":" << errors.size() << "}";
      fs::remove_all(dir, ec2);
      res.status = 422;
      res.set_content(os.str(), "application/json");
      return;
    }
    std::ostringstream os;
    os << "{" << Q("instance_id") << ":" << Q(id) << "," << Q("accepted") << ":true,"
       << Q("summary") << ":" << SummariseInstance(inst) << "}";
    res.set_content(os.str(), "application/json");
  });

  srv.Post("/api/v1/jobs", [](const httplib::Request& req, httplib::Response& res) {
    if (!Authorised(req)) return Deny(res, 401, "authentication required");
    const std::string inst = req.get_param_value("instance_id");
    std::string scenario = req.get_param_value("scenario");
    if (scenario.empty()) scenario = "all";
    if (!SafeId(inst) || !fs::exists(InstanceDir(inst)))
      return Deny(res, 404, "unknown instance_id");
    if (scenario != "all" && !ta::ParseScenario(scenario))
      return Deny(res, 400, "scenario must be A, B, C or all");
    double seconds = 60;
    if (!req.get_param_value("seconds").empty()) {
      try { seconds = std::stod(req.get_param_value("seconds")); } catch (...) {}
    }
    seconds = std::clamp(seconds, 1.0, g_cfg.max_solve_seconds);

    auto job = std::make_shared<Job>();
    job->id = RandomId();
    job->instance_id = inst;
    job->scenario = scenario;
    job->seconds = seconds;
    job->created_at = NowIso();
    {
      std::lock_guard<std::mutex> lk(g_mu);
      if (static_cast<int>(g_queue.size()) >= g_cfg.max_queue)
        return Deny(res, 429, "the solve queue is full; try again shortly");
      g_jobs[job->id] = job;
      g_queue.push_back(job->id);
    }
    g_cv.notify_all();
    res.set_content(JobJson(*job), "application/json");
  });

  srv.Get(R"(/api/v1/jobs/([a-z0-9]+))", [](const httplib::Request& req, httplib::Response& res) {
    if (!Authorised(req)) return Deny(res, 401, "authentication required");
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_jobs.find(req.matches[1]);
    if (it == g_jobs.end()) return Deny(res, 404, "unknown job");
    res.set_content(JobJson(*it->second), "application/json");
  });

  srv.Post(R"(/api/v1/jobs/([a-z0-9]+)/cancel)", [](const httplib::Request& req, httplib::Response& res) {
    if (!Authorised(req)) return Deny(res, 401, "authentication required");
    std::shared_ptr<Job> job;
    {
      std::lock_guard<std::mutex> lk(g_mu);
      auto it = g_jobs.find(req.matches[1]);
      if (it == g_jobs.end()) return Deny(res, 404, "unknown job");
      job = it->second;
      job->cancel.store(true);
      if (job->pid > 0) ::kill(job->pid, SIGTERM);
      if (job->state == "queued") { job->state = "cancelled"; job->finished_at = NowIso(); }
    }
    res.set_content(JobJson(*job), "application/json");
  });

  // Validation report for one scenario of a finished job.
  srv.Get(R"(/api/v1/jobs/([a-z0-9]+)/validation/([ABC]))",
          [](const httplib::Request& req, httplib::Response& res) {
    if (!Authorised(req)) return Deny(res, 401, "authentication required");
    const std::string p = JobDir(req.matches[1]) + "/" + std::string(req.matches[2]) + "/VALIDATION.json";
    std::string body;
    if (!ta::ReadFile(p, &body)) return Deny(res, 404, "no validation report for that scenario");
    res.set_content(body, "application/json");
  });

  // Competition output files. Only the three canonical names are servable.
  srv.Get(R"(/api/v1/jobs/([a-z0-9]+)/files/([ABC])/([A-Z_]+\.csv))",
          [](const httplib::Request& req, httplib::Response& res) {
    if (!Authorised(req)) return Deny(res, 401, "authentication required");
    const std::string name = req.matches[3];
    if (name != "SCHEDULE_ACCESS.csv" && name != "SCHEDULE_OCCUPANCY.csv" && name != "RESULTS.csv")
      return Deny(res, 404, "not a competition output file");
    std::string body;
    if (!ta::ReadFile(JobDir(req.matches[1]) + "/" + std::string(req.matches[2]) + "/" + name, &body))
      return Deny(res, 404, "file not produced");
    res.set_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
    res.set_content(body, "text/csv");
  });

  srv.Get(R"(/api/v1/jobs/([a-z0-9]+)/log)", [](const httplib::Request& req, httplib::Response& res) {
    if (!Authorised(req)) return Deny(res, 401, "authentication required");
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_jobs.find(req.matches[1]);
    if (it == g_jobs.end()) return Deny(res, 404, "unknown job");
    res.set_content(it->second->log, "text/plain");
  });

  // Full instance detail for the interface: network, contracts, activities with
  // their expanded spans and closure zones. Canonical ids throughout.
  srv.Get(R"(/api/v1/instances/([a-z0-9]+)/detail)",
          [](const httplib::Request& req, httplib::Response& res) {
    if (!Authorised(req)) return Deny(res, 401, "authentication required");
    const std::string id = req.matches[1];
    if (!SafeId(id) || !fs::exists(InstanceDir(id))) return Deny(res, 404, "unknown instance");
    ta::Instance inst;
    std::vector<ta::InputError> errs;
    if (!ta::LoadInstance(InstanceDir(id), &inst, &errs)) return Deny(res, 500, "instance no longer loads");
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
      for (size_t k = 0; k < a.closure.size(); ++k)
        os << (k ? "," : "") << Q(inst.locations[a.closure[k]].id);
      os << "]}";
    }
    os << "]}";
    res.set_content(os.str(), "application/json");
  });

  // One-click load of the bundled public instance, so a reviewer can exercise
  // the whole workflow without hunting for files. Clearly a bundled dataset,
  // never presented as a live upload.
  srv.Post("/api/v1/instances/demo", [](const httplib::Request& req, httplib::Response& res) {
    if (!Authorised(req)) return Deny(res, 401, "authentication required");
    if (g_cfg.public_instance.empty() || !fs::exists(g_cfg.public_instance))
      return Deny(res, 404, "no public instance is bundled with this deployment");
    const std::string id = RandomId();
    const std::string dir = InstanceDir(id);
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
    std::ostringstream os;
    os << "{" << Q("instance_id") << ":" << Q(id) << "," << Q("accepted") << ":true,"
       << Q("bundled") << ":true," << Q("summary") << ":" << SummariseInstance(inst) << "}";
    res.set_content(os.str(), "application/json");
  });

  srv.set_mount_point("/", g_cfg.web);

  std::vector<std::thread> pool;
  for (int i = 0; i < g_cfg.max_concurrent_solves; ++i) pool.emplace_back(WorkerLoop);

  std::cout << "trackaccess-service listening on http://" << g_cfg.host << ":" << g_cfg.port << "\n"
            << "  store       " << fs::absolute(g_cfg.root).string() << "\n"
            << "  web root    " << fs::absolute(g_cfg.web).string() << "\n"
            << "  worker      " << g_cfg.worker << "\n"
            << "  auth        " << (g_cfg.require_auth ? "bearer token" : "DISABLED (--auth none)") << "\n";
  if (g_cfg.require_auth) std::cout << "  token       " << g_cfg.token << "\n";
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
