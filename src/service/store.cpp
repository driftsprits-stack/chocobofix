#include "service/store.h"

#include <sqlite3.h>

#include <ctime>
#include <memory>
#include <sstream>

#include "core/csv.h"   // ta::Sha256Hex, for content and token hashing
#include "service/crypto.h"

namespace ta {
namespace {

std::string NowIso() {
  const auto t = std::time(nullptr);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
  return buf;
}

// RAII for a prepared statement, so no path can leak one.
class Stmt {
 public:
  Stmt(sqlite3* db, const std::string& sql) { ok_ = sqlite3_prepare_v2(db, sql.c_str(), -1, &s_, nullptr) == SQLITE_OK; }
  ~Stmt() { if (s_) sqlite3_finalize(s_); }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;
  bool ok() const { return ok_; }
  sqlite3_stmt* get() { return s_; }
  void Bind(int i, const std::string& v) { sqlite3_bind_text(s_, i, v.c_str(), -1, SQLITE_TRANSIENT); }
  void Bind(int i, long long v) { sqlite3_bind_int64(s_, i, v); }
  void Bind(int i, int v) { sqlite3_bind_int(s_, i, v); }
  bool Step() { return sqlite3_step(s_) == SQLITE_ROW; }
  bool Done() { return sqlite3_step(s_) == SQLITE_DONE; }
  std::string Text(int c) {
    const unsigned char* p = sqlite3_column_text(s_, c);
    return p ? reinterpret_cast<const char*>(p) : "";
  }
  long long Int64(int c) { return sqlite3_column_int64(s_, c); }
  int Int(int c) { return sqlite3_column_int(s_, c); }

 private:
  sqlite3_stmt* s_ = nullptr;
  bool ok_ = false;
};

User ReadUser(Stmt& q) {
  User u;
  u.id = q.Int64(0);
  u.username = q.Text(1);
  u.role = ParseRole(q.Text(2)).value_or(Role::kViewer);
  u.disabled = q.Int(3) != 0;
  u.created_at = q.Text(4);
  return u;
}

PlanVersion ReadPv(Stmt& q) {
  PlanVersion p;
  p.id = q.Int64(0); p.project_id = q.Int64(1); p.instance_id = q.Int64(2);
  p.scenario = q.Text(3); p.version_no = q.Int(4); p.created_by = q.Int64(5);
  p.created_at = q.Text(6); p.dir = q.Text(7);
  p.feasible = q.Int(8) != 0; p.violations = q.Int(9); p.objective_tenths = q.Int64(10);
  p.content_hash = q.Text(11); p.validation_hash = q.Text(12); p.input_hash = q.Text(13);
  p.is_fallback = q.Int(14) != 0; p.strict_buffers = q.Int(15) != 0;
  p.status = q.Text(16); p.approved_by = q.Int64(17); p.approved_at = q.Text(18);
  return p;
}
const char* kPvCols =
    "id,project_id,instance_id,scenario,version_no,created_by,created_at,dir,"
    "feasible,violations,objective_tenths,content_hash,validation_hash,input_hash,"
    "is_fallback,strict_buffers,status,approved_by,approved_at";

}  // namespace

std::string_view ToString(Role r) {
  switch (r) {
    case Role::kAdministrator: return "administrator";
    case Role::kApprover: return "approver";
    case Role::kPlanner: return "planner";
    default: return "viewer";
  }
}
std::optional<Role> ParseRole(std::string_view s) {
  if (s == "administrator") return Role::kAdministrator;
  if (s == "approver") return Role::kApprover;
  if (s == "planner") return Role::kPlanner;
  if (s == "viewer") return Role::kViewer;
  return std::nullopt;
}

// The permission matrix, in one readable place. Note the two deliberate gaps:
// an administrator cannot approve a plan, and an approver cannot create one.
// Separating those duties is the point; neither is an oversight.
bool RoleHas(Role r, Cap c) {
  switch (c) {
    case Cap::kViewProject:    return true;                       // every authenticated role
    case Cap::kCreateProject:
    case Cap::kUploadInstance:
    case Cap::kRunSolve:       return r == Role::kPlanner || r == Role::kAdministrator;
    case Cap::kApprovePlan:    return r == Role::kApprover;       // NOT administrator
    case Cap::kManageUsers:    return r == Role::kAdministrator;
    case Cap::kViewAudit:      return r == Role::kAdministrator || r == Role::kApprover;
  }
  return false;
}

Store::~Store() { if (db_) sqlite3_close(db_); }

bool Store::Exec(const std::string& sql, std::string* err) {
  char* msg = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &msg) != SQLITE_OK) {
    if (err) *err = msg ? msg : "sqlite error";
    if (msg) sqlite3_free(msg);
    return false;
  }
  return true;
}

bool Store::Open(const std::string& path, std::string* err) {
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
    *err = "cannot open database at " + path;
    return false;
  }
  // WAL keeps readers from blocking the writer; FULL synchronous means a commit
  // is on disk before it is acknowledged. busy_timeout covers the brief overlap
  // when a reader and the writer contend.
  if (!Exec("PRAGMA journal_mode=WAL;"
            "PRAGMA synchronous=FULL;"
            "PRAGMA foreign_keys=ON;"
            "PRAGMA busy_timeout=5000;", err)) return false;

  return Exec(R"SQL(
    CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS users (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      username TEXT NOT NULL UNIQUE,
      password_hash TEXT NOT NULL,
      role TEXT NOT NULL,
      disabled INTEGER NOT NULL DEFAULT 0,
      created_at TEXT NOT NULL);
    CREATE TABLE IF NOT EXISTS sessions (
      token_hash TEXT PRIMARY KEY,
      user_id INTEGER NOT NULL REFERENCES users(id),
      created_at TEXT NOT NULL,
      last_seen TEXT NOT NULL,
      idle_expires_at INTEGER NOT NULL,
      absolute_expires_at INTEGER NOT NULL);
    CREATE INDEX IF NOT EXISTS ix_sessions_user ON sessions(user_id);
    CREATE TABLE IF NOT EXISTS projects (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      name TEXT NOT NULL,
      owner_id INTEGER NOT NULL REFERENCES users(id),
      revision INTEGER NOT NULL DEFAULT 1,
      created_at TEXT NOT NULL);
    CREATE TABLE IF NOT EXISTS instances (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      project_id INTEGER NOT NULL REFERENCES projects(id),
      dir TEXT NOT NULL,
      input_hash TEXT NOT NULL,
      uploaded_by INTEGER NOT NULL REFERENCES users(id),
      created_at TEXT NOT NULL,
      label TEXT NOT NULL DEFAULT '');
    CREATE INDEX IF NOT EXISTS ix_inst_project ON instances(project_id);
    CREATE TABLE IF NOT EXISTS plan_versions (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      project_id INTEGER NOT NULL REFERENCES projects(id),
      instance_id INTEGER NOT NULL REFERENCES instances(id),
      scenario TEXT NOT NULL,
      version_no INTEGER NOT NULL,
      created_by INTEGER NOT NULL REFERENCES users(id),
      created_at TEXT NOT NULL,
      dir TEXT NOT NULL,
      feasible INTEGER NOT NULL,
      violations INTEGER NOT NULL,
      objective_tenths INTEGER NOT NULL,
      content_hash TEXT NOT NULL,
      validation_hash TEXT NOT NULL,
      input_hash TEXT NOT NULL,
      is_fallback INTEGER NOT NULL DEFAULT 0,
      strict_buffers INTEGER NOT NULL DEFAULT 0,
      status TEXT NOT NULL DEFAULT 'draft',
      approved_by INTEGER NOT NULL DEFAULT 0,
      approved_at TEXT NOT NULL DEFAULT '');
    CREATE INDEX IF NOT EXISTS ix_pv_project ON plan_versions(project_id);
    CREATE TABLE IF NOT EXISTS approvals (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      plan_version_id INTEGER NOT NULL REFERENCES plan_versions(id),
      approved_by INTEGER NOT NULL REFERENCES users(id),
      approved_at TEXT NOT NULL,
      content_hash TEXT NOT NULL,
      validation_hash TEXT NOT NULL,
      revoked INTEGER NOT NULL DEFAULT 0,
      revoked_by INTEGER NOT NULL DEFAULT 0,
      revoked_at TEXT NOT NULL DEFAULT '',
      revoke_reason TEXT NOT NULL DEFAULT '');
    CREATE TABLE IF NOT EXISTS audit (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      ts TEXT NOT NULL,
      actor_id INTEGER NOT NULL,
      action TEXT NOT NULL,
      object_type TEXT NOT NULL,
      object_id TEXT NOT NULL,
      result TEXT NOT NULL,
      correlation_id TEXT NOT NULL,
      detail TEXT NOT NULL);
    CREATE INDEX IF NOT EXISTS ix_audit_object ON audit(object_type, object_id);
  )SQL", err);
}

// --- users -----------------------------------------------------------------
bool Store::CreateUser(const std::string& username, const std::string& password, Role role,
                       User* out, std::string* err) {
  if (username.empty() || username.size() > 64) { *err = "username must be 1..64 characters"; return false; }
  for (char c : username)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '_' && c != '-') {
      *err = "username may contain only letters, digits, dot, underscore and hyphen";
      return false;
    }
  if (password.size() < 12) { *err = "password must be at least 12 characters"; return false; }
  if (password.size() > 1024) { *err = "password is too long"; return false; }

  Stmt q(db_, "INSERT INTO users(username,password_hash,role,disabled,created_at) VALUES(?,?,?,0,?)");
  if (!q.ok()) { *err = "prepare failed"; return false; }
  q.Bind(1, username);
  q.Bind(2, HashPassword(password));
  q.Bind(3, std::string(ToString(role)));
  q.Bind(4, NowIso());
  if (!q.Done()) { *err = "username already exists"; return false; }
  if (out) { auto u = FindUser(username); if (u) *out = *u; }
  return true;
}

std::optional<User> Store::FindUser(const std::string& username) {
  Stmt q(db_, "SELECT id,username,role,disabled,created_at FROM users WHERE username=?");
  if (!q.ok()) return std::nullopt;
  q.Bind(1, username);
  if (!q.Step()) return std::nullopt;
  return ReadUser(q);
}

std::optional<User> Store::UserById(long long id) {
  Stmt q(db_, "SELECT id,username,role,disabled,created_at FROM users WHERE id=?");
  if (!q.ok()) return std::nullopt;
  q.Bind(1, id);
  if (!q.Step()) return std::nullopt;
  return ReadUser(q);
}

std::vector<User> Store::ListUsers() {
  std::vector<User> out;
  Stmt q(db_, "SELECT id,username,role,disabled,created_at FROM users ORDER BY username");
  if (!q.ok()) return out;
  while (q.Step()) out.push_back(ReadUser(q));
  return out;
}

bool Store::SetUserDisabled(long long id, bool disabled, std::string* err) {
  Stmt q(db_, "UPDATE users SET disabled=? WHERE id=?");
  if (!q.ok()) { *err = "prepare failed"; return false; }
  q.Bind(1, disabled ? 1 : 0);
  q.Bind(2, id);
  if (!q.Done()) { *err = "update failed"; return false; }
  if (disabled) RevokeAllSessionsFor(id);   // a disabled account loses its sessions at once
  return true;
}

bool Store::SetUserRole(long long id, Role role, std::string* err) {
  Stmt q(db_, "UPDATE users SET role=? WHERE id=?");
  if (!q.ok()) { *err = "prepare failed"; return false; }
  q.Bind(1, std::string(ToString(role)));
  q.Bind(2, id);
  if (!q.Done()) { *err = "update failed"; return false; }
  // A role change must not leave a session running with the old capabilities.
  RevokeAllSessionsFor(id);
  return true;
}

bool Store::SetPassword(long long id, const std::string& password, std::string* err) {
  if (password.size() < 12) { *err = "password must be at least 12 characters"; return false; }
  Stmt q(db_, "UPDATE users SET password_hash=? WHERE id=?");
  if (!q.ok()) { *err = "prepare failed"; return false; }
  q.Bind(1, HashPassword(password));
  q.Bind(2, id);
  if (!q.Done()) { *err = "update failed"; return false; }
  RevokeAllSessionsFor(id);
  return true;
}

std::optional<User> Store::Authenticate(const std::string& username, const std::string& password) {
  Stmt q(db_, "SELECT id,username,role,disabled,created_at,password_hash FROM users WHERE username=?");
  if (!q.ok()) return std::nullopt;
  q.Bind(1, username);
  if (!q.Step()) {
    // Spend comparable time on an unknown user so the response does not reveal
    // whether the account exists.
    VerifyPassword(password, "pbkdf2_sha256$210000$00$00");
    return std::nullopt;
  }
  User u = ReadUser(q);
  const std::string stored = q.Text(5);
  if (!VerifyPassword(password, stored)) return std::nullopt;
  if (u.disabled) return std::nullopt;
  return u;
}

int Store::UserCount() {
  Stmt q(db_, "SELECT COUNT(*) FROM users");
  if (!q.ok() || !q.Step()) return 0;
  return q.Int(0);
}

// --- sessions --------------------------------------------------------------
std::string Store::CreateSession(long long user_id, int idle_seconds, int absolute_seconds) {
  const std::string token = RandomToken(32);
  const long long now = static_cast<long long>(std::time(nullptr));
  Stmt q(db_, "INSERT INTO sessions(token_hash,user_id,created_at,last_seen,idle_expires_at,"
              "absolute_expires_at) VALUES(?,?,?,?,?,?)");
  if (!q.ok()) return "";
  q.Bind(1, Sha256Hex(token));      // only the hash is stored
  q.Bind(2, user_id);
  q.Bind(3, NowIso());
  q.Bind(4, NowIso());
  q.Bind(5, now + idle_seconds);
  q.Bind(6, now + absolute_seconds);
  if (!q.Done()) return "";
  return token;
}

std::optional<User> Store::UserForSession(const std::string& token) {
  if (token.empty()) return std::nullopt;
  const std::string h = Sha256Hex(token);
  const long long now = static_cast<long long>(std::time(nullptr));
  long long user_id = 0, idle_exp = 0, abs_exp = 0;
  {
    Stmt q(db_, "SELECT user_id,idle_expires_at,absolute_expires_at FROM sessions WHERE token_hash=?");
    if (!q.ok()) return std::nullopt;
    q.Bind(1, h);
    if (!q.Step()) return std::nullopt;
    user_id = q.Int64(0); idle_exp = q.Int64(1); abs_exp = q.Int64(2);
  }
  if (now > idle_exp || now > abs_exp) {
    Stmt d(db_, "DELETE FROM sessions WHERE token_hash=?");
    if (d.ok()) { d.Bind(1, h); d.Done(); }
    return std::nullopt;
  }
  auto u = UserById(user_id);
  if (!u || u->disabled) return std::nullopt;
  // Sliding idle window, capped by the absolute expiry which never moves.
  Stmt up(db_, "UPDATE sessions SET last_seen=?, idle_expires_at=? WHERE token_hash=?");
  if (up.ok()) {
    up.Bind(1, NowIso());
    up.Bind(2, std::min<long long>(now + (idle_exp - now > 0 ? 1800 : 1800), abs_exp));
    up.Bind(3, h);
    up.Done();
  }
  return u;
}

void Store::RevokeSession(const std::string& token) {
  Stmt q(db_, "DELETE FROM sessions WHERE token_hash=?");
  if (!q.ok()) return;
  q.Bind(1, Sha256Hex(token));
  q.Done();
}

void Store::RevokeAllSessionsFor(long long user_id) {
  Stmt q(db_, "DELETE FROM sessions WHERE user_id=?");
  if (!q.ok()) return;
  q.Bind(1, user_id);
  q.Done();
}

int Store::PurgeExpiredSessions() {
  const long long now = static_cast<long long>(std::time(nullptr));
  Stmt q(db_, "DELETE FROM sessions WHERE idle_expires_at < ? OR absolute_expires_at < ?");
  if (!q.ok()) return 0;
  q.Bind(1, now); q.Bind(2, now);
  q.Done();
  return sqlite3_changes(db_);
}

// --- projects --------------------------------------------------------------
bool Store::CreateProject(const std::string& name, long long owner_id, Project* out, std::string* err) {
  if (name.empty() || name.size() > 120) { *err = "project name must be 1..120 characters"; return false; }
  Stmt q(db_, "INSERT INTO projects(name,owner_id,revision,created_at) VALUES(?,?,1,?)");
  if (!q.ok()) { *err = "prepare failed"; return false; }
  q.Bind(1, name); q.Bind(2, owner_id); q.Bind(3, NowIso());
  if (!q.Done()) { *err = "insert failed"; return false; }
  if (out) {
    auto p = ProjectById(sqlite3_last_insert_rowid(db_));
    if (p) *out = *p;
  }
  return true;
}

std::optional<Project> Store::ProjectById(long long id) {
  Stmt q(db_, "SELECT id,name,owner_id,revision,created_at FROM projects WHERE id=?");
  if (!q.ok()) return std::nullopt;
  q.Bind(1, id);
  if (!q.Step()) return std::nullopt;
  Project p;
  p.id = q.Int64(0); p.name = q.Text(1); p.owner_id = q.Int64(2);
  p.revision = q.Int64(3); p.created_at = q.Text(4);
  return p;
}

std::vector<Project> Store::ListProjects() {
  std::vector<Project> out;
  Stmt q(db_, "SELECT id,name,owner_id,revision,created_at FROM projects ORDER BY id DESC");
  if (!q.ok()) return out;
  while (q.Step()) {
    Project p;
    p.id = q.Int64(0); p.name = q.Text(1); p.owner_id = q.Int64(2);
    p.revision = q.Int64(3); p.created_at = q.Text(4);
    out.push_back(p);
  }
  return out;
}

// Compare-and-swap on the revision. A stale writer is rejected rather than
// silently overwriting whatever arrived first.
bool Store::BumpProjectRevision(long long id, long long expected_revision, std::string* err) {
  Stmt q(db_, "UPDATE projects SET revision=revision+1 WHERE id=? AND revision=?");
  if (!q.ok()) { *err = "prepare failed"; return false; }
  q.Bind(1, id); q.Bind(2, expected_revision);
  if (!q.Done()) { *err = "update failed"; return false; }
  if (sqlite3_changes(db_) == 0) {
    auto cur = ProjectById(id);
    *err = "the project changed since you loaded it (you had revision " +
           std::to_string(expected_revision) + ", it is now " +
           (cur ? std::to_string(cur->revision) : std::string("unknown")) + ")";
    return false;
  }
  return true;
}

// --- instances -------------------------------------------------------------
bool Store::AddInstance(const InstanceRec& rec, InstanceRec* out, std::string* err) {
  Stmt q(db_, "INSERT INTO instances(project_id,dir,input_hash,uploaded_by,created_at,label) "
              "VALUES(?,?,?,?,?,?)");
  if (!q.ok()) { *err = "prepare failed"; return false; }
  q.Bind(1, rec.project_id); q.Bind(2, rec.dir); q.Bind(3, rec.input_hash);
  q.Bind(4, rec.uploaded_by); q.Bind(5, NowIso()); q.Bind(6, rec.label);
  if (!q.Done()) { *err = "insert failed"; return false; }
  if (out) { auto r = InstanceById(sqlite3_last_insert_rowid(db_)); if (r) *out = *r; }
  return true;
}

std::optional<InstanceRec> Store::InstanceById(long long id) {
  Stmt q(db_, "SELECT id,project_id,dir,input_hash,uploaded_by,created_at,label FROM instances WHERE id=?");
  if (!q.ok()) return std::nullopt;
  q.Bind(1, id);
  if (!q.Step()) return std::nullopt;
  InstanceRec r;
  r.id = q.Int64(0); r.project_id = q.Int64(1); r.dir = q.Text(2); r.input_hash = q.Text(3);
  r.uploaded_by = q.Int64(4); r.created_at = q.Text(5); r.label = q.Text(6);
  return r;
}

std::vector<InstanceRec> Store::ListInstances(long long project_id) {
  std::vector<InstanceRec> out;
  Stmt q(db_, "SELECT id,project_id,dir,input_hash,uploaded_by,created_at,label FROM instances "
              "WHERE project_id=? ORDER BY id DESC");
  if (!q.ok()) return out;
  q.Bind(1, project_id);
  while (q.Step()) {
    InstanceRec r;
    r.id = q.Int64(0); r.project_id = q.Int64(1); r.dir = q.Text(2); r.input_hash = q.Text(3);
    r.uploaded_by = q.Int64(4); r.created_at = q.Text(5); r.label = q.Text(6);
    out.push_back(r);
  }
  return out;
}

// --- plan versions ---------------------------------------------------------
bool Store::AddPlanVersion(const PlanVersion& pv, PlanVersion* out, std::string* err) {
  int next = 1;
  {
    Stmt q(db_, "SELECT COALESCE(MAX(version_no),0)+1 FROM plan_versions WHERE project_id=? AND scenario=?");
    if (!q.ok()) { *err = "prepare failed"; return false; }
    q.Bind(1, pv.project_id); q.Bind(2, pv.scenario);
    if (q.Step()) next = q.Int(0);
  }
  Stmt q(db_, "INSERT INTO plan_versions(project_id,instance_id,scenario,version_no,created_by,"
              "created_at,dir,feasible,violations,objective_tenths,content_hash,validation_hash,"
              "input_hash,is_fallback,strict_buffers,status,approved_by,approved_at) "
              "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?, 'draft',0,'')");
  if (!q.ok()) { *err = "prepare failed"; return false; }
  q.Bind(1, pv.project_id); q.Bind(2, pv.instance_id); q.Bind(3, pv.scenario);
  q.Bind(4, next); q.Bind(5, pv.created_by); q.Bind(6, NowIso()); q.Bind(7, pv.dir);
  q.Bind(8, pv.feasible ? 1 : 0); q.Bind(9, pv.violations); q.Bind(10, pv.objective_tenths);
  q.Bind(11, pv.content_hash); q.Bind(12, pv.validation_hash); q.Bind(13, pv.input_hash);
  q.Bind(14, pv.is_fallback ? 1 : 0); q.Bind(15, pv.strict_buffers ? 1 : 0);
  if (!q.Done()) { *err = "insert failed"; return false; }
  if (out) { auto r = PlanVersionById(sqlite3_last_insert_rowid(db_)); if (r) *out = *r; }
  return true;
}

std::optional<PlanVersion> Store::PlanVersionById(long long id) {
  Stmt q(db_, std::string("SELECT ") + kPvCols + " FROM plan_versions WHERE id=?");
  if (!q.ok()) return std::nullopt;
  q.Bind(1, id);
  if (!q.Step()) return std::nullopt;
  return ReadPv(q);
}

std::vector<PlanVersion> Store::ListPlanVersions(long long project_id) {
  std::vector<PlanVersion> out;
  Stmt q(db_, std::string("SELECT ") + kPvCols +
              " FROM plan_versions WHERE project_id=? ORDER BY id DESC");
  if (!q.ok()) return out;
  q.Bind(1, project_id);
  while (q.Step()) out.push_back(ReadPv(q));
  return out;
}

bool Store::ApprovePlan(long long version_id, long long approver_id,
                        const std::string& expect_content_hash,
                        const std::string& expect_validation_hash, std::string* err) {
  auto pv = PlanVersionById(version_id);
  if (!pv) { *err = "no such plan version"; return false; }

  // A plan that breaches a hard rule can never be approved, whatever the
  // approver intends. This is enforced here, in the store, so no interface or
  // API path can route around it.
  if (!pv->feasible) {
    *err = "this plan has " + std::to_string(pv->violations) +
           " hard violations and cannot be approved";
    return false;
  }
  if (pv->is_fallback) {
    *err = "this plan was produced in fallback mode and breaches its scenario's own "
           "policy; it is not submission-ready and cannot be approved";
    return false;
  }
  if (pv->status == "invalidated") {
    *err = "this plan was produced from an input that is no longer current; "
           "re-run it against the current input before approving";
    return false;
  }
  if (pv->status == "approved") { *err = "this plan version is already approved"; return false; }
  // Approval is bound to exactly what the approver was shown. If either hash
  // fails to match, they were looking at something else.
  if (!expect_content_hash.empty() && !ConstantTimeEquals(expect_content_hash, pv->content_hash)) {
    *err = "the plan content changed since it was displayed; reload and check it again";
    return false;
  }
  if (!expect_validation_hash.empty() &&
      !ConstantTimeEquals(expect_validation_hash, pv->validation_hash)) {
    *err = "the validation result changed since it was displayed; reload and check it again";
    return false;
  }

  if (!Exec("BEGIN IMMEDIATE", err)) return false;
  // Guard against two approvers racing: the UPDATE only fires while the row is
  // still a draft, so the loser gets zero changed rows rather than a second
  // approval.
  {
    Stmt q(db_, "UPDATE plan_versions SET status='approved', approved_by=?, approved_at=? "
                "WHERE id=? AND status='draft'");
    if (!q.ok()) { Exec("ROLLBACK", nullptr); *err = "prepare failed"; return false; }
    q.Bind(1, approver_id); q.Bind(2, NowIso()); q.Bind(3, version_id);
    q.Done();
    if (sqlite3_changes(db_) == 0) {
      Exec("ROLLBACK", nullptr);
      *err = "this plan version was approved by someone else a moment ago";
      return false;
    }
  }
  {
    Stmt q(db_, "INSERT INTO approvals(plan_version_id,approved_by,approved_at,content_hash,"
                "validation_hash) VALUES(?,?,?,?,?)");
    if (!q.ok()) { Exec("ROLLBACK", nullptr); *err = "prepare failed"; return false; }
    q.Bind(1, version_id); q.Bind(2, approver_id); q.Bind(3, NowIso());
    q.Bind(4, pv->content_hash); q.Bind(5, pv->validation_hash);
    if (!q.Done()) { Exec("ROLLBACK", nullptr); *err = "insert failed"; return false; }
  }
  // Only one version per scenario may stand approved; earlier ones are superseded.
  {
    Stmt q(db_, "UPDATE plan_versions SET status='superseded' WHERE project_id=? AND scenario=? "
                "AND id<>? AND status='approved'");
    if (q.ok()) { q.Bind(1, pv->project_id); q.Bind(2, pv->scenario); q.Bind(3, version_id); q.Done(); }
  }
  return Exec("COMMIT", err);
}

bool Store::RevokeApproval(long long version_id, long long actor_id, const std::string& reason,
                           std::string* err) {
  auto pv = PlanVersionById(version_id);
  if (!pv) { *err = "no such plan version"; return false; }
  if (pv->status != "approved") { *err = "this plan version is not approved"; return false; }
  if (!Exec("BEGIN IMMEDIATE", err)) return false;
  {
    Stmt q(db_, "UPDATE plan_versions SET status='draft', approved_by=0, approved_at='' WHERE id=?");
    if (!q.ok()) { Exec("ROLLBACK", nullptr); *err = "prepare failed"; return false; }
    q.Bind(1, version_id);
    q.Done();
  }
  {
    Stmt q(db_, "UPDATE approvals SET revoked=1, revoked_by=?, revoked_at=?, revoke_reason=? "
                "WHERE plan_version_id=? AND revoked=0");
    if (q.ok()) { q.Bind(1, actor_id); q.Bind(2, NowIso()); q.Bind(3, reason); q.Bind(4, version_id); q.Done(); }
  }
  return Exec("COMMIT", err);
}

std::vector<Approval> Store::ListApprovals(long long project_id) {
  std::vector<Approval> out;
  Stmt q(db_, "SELECT a.id,a.plan_version_id,a.approved_by,a.approved_at,a.content_hash,"
              "a.validation_hash,a.revoked,a.revoked_by,a.revoked_at,a.revoke_reason "
              "FROM approvals a JOIN plan_versions p ON p.id=a.plan_version_id "
              "WHERE p.project_id=? ORDER BY a.id DESC");
  if (!q.ok()) return out;
  q.Bind(1, project_id);
  while (q.Step()) {
    Approval a;
    a.id = q.Int64(0); a.plan_version_id = q.Int64(1); a.approved_by = q.Int64(2);
    a.approved_at = q.Text(3); a.content_hash = q.Text(4); a.validation_hash = q.Text(5);
    a.revoked = q.Int(6) != 0; a.revoked_by = q.Int64(7); a.revoked_at = q.Text(8);
    a.revoke_reason = q.Text(9);
    out.push_back(a);
  }
  return out;
}

// A new instance makes every plan built on an older one outdated. They are not
// deleted - the history stays - but they stop counting as approved, because they
// were approved for conditions that no longer hold.
int Store::InvalidateApprovalsForChangedInput(long long project_id, const std::string& new_input_hash,
                                              long long actor_id) {
  Stmt q(db_, "UPDATE plan_versions SET status='invalidated' WHERE project_id=? "
              "AND input_hash<>? AND status IN ('approved','draft')");
  if (!q.ok()) return 0;
  q.Bind(1, project_id); q.Bind(2, new_input_hash);
  q.Done();
  const int n = sqlite3_changes(db_);
  if (n > 0) {
    Stmt r(db_, "UPDATE approvals SET revoked=1, revoked_by=?, revoked_at=?, "
                "revoke_reason='the input it was approved against was replaced' "
                "WHERE revoked=0 AND plan_version_id IN "
                "(SELECT id FROM plan_versions WHERE project_id=? AND input_hash<>?)");
    if (r.ok()) { r.Bind(1, actor_id); r.Bind(2, NowIso()); r.Bind(3, project_id);
                  r.Bind(4, new_input_hash); r.Done(); }
  }
  return n;
}

// --- audit -----------------------------------------------------------------
void Store::Audit(long long actor_id, const std::string& action, const std::string& object_type,
                  const std::string& object_id, const std::string& result,
                  const std::string& correlation_id, const std::string& detail) {
  Stmt q(db_, "INSERT INTO audit(ts,actor_id,action,object_type,object_id,result,correlation_id,"
              "detail) VALUES(?,?,?,?,?,?,?,?)");
  if (!q.ok()) return;
  q.Bind(1, NowIso()); q.Bind(2, actor_id); q.Bind(3, action); q.Bind(4, object_type);
  q.Bind(5, object_id); q.Bind(6, result); q.Bind(7, correlation_id); q.Bind(8, detail);
  q.Done();
}

std::vector<AuditEvent> Store::ListAudit(int limit, const std::string& object_type,
                                         const std::string& object_id) {
  std::vector<AuditEvent> out;
  std::string sql = "SELECT a.id,a.ts,a.actor_id,COALESCE(u.username,''),a.action,a.object_type,"
                    "a.object_id,a.result,a.correlation_id,a.detail FROM audit a "
                    "LEFT JOIN users u ON u.id=a.actor_id";
  if (!object_type.empty()) sql += " WHERE a.object_type=? AND a.object_id=?";
  sql += " ORDER BY a.id DESC LIMIT ?";
  Stmt q(db_, sql);
  if (!q.ok()) return out;
  int i = 1;
  if (!object_type.empty()) { q.Bind(i++, object_type); q.Bind(i++, object_id); }
  q.Bind(i, limit);
  while (q.Step()) {
    AuditEvent e;
    e.id = q.Int64(0); e.ts = q.Text(1); e.actor_id = q.Int64(2); e.actor_name = q.Text(3);
    e.action = q.Text(4); e.object_type = q.Text(5); e.object_id = q.Text(6);
    e.result = q.Text(7); e.correlation_id = q.Text(8); e.detail = q.Text(9);
    out.push_back(e);
  }
  return out;
}

}  // namespace ta
