// Shared-project persistence: accounts, sessions, projects, instances, plan
// versions, approvals and an audit trail, in one SQLite database.
//
// The service process is the only writer. Clients never open the database, so a
// network share can never end up with two writers. Every mutating call is
// wrapped in a transaction and reports success only after it commits.
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace ta {

// Single-valued, deliberately. Administrative access does NOT imply approval:
// the brief requires those duties to be separable, so an administrator manages
// accounts and an approver signs plans off, and neither can do the other's job.
enum class Role { kViewer, kPlanner, kApprover, kAdministrator };

std::string_view ToString(Role r);
std::optional<Role> ParseRole(std::string_view s);

// Capabilities are named, not inferred from role ordering, so the matrix is
// readable and testable in one place.
enum class Cap {
  kViewProject,
  kCreateProject,
  kUploadInstance,
  kRunSolve,
  kApprovePlan,
  kManageUsers,
  kViewAudit,
};
bool RoleHas(Role r, Cap c);

struct Assignment {
  long long project_id = 0;
  long long instance_id = 0;
  std::string activity_id;
  long long coordinator_id = 0;
  std::string coordinator_name;
  long long assigned_by = 0;
  std::string assigned_at;
};

struct User {
  long long id = 0;
  std::string username;
  Role role = Role::kViewer;
  bool disabled = false;
  std::string created_at;
};

struct Project {
  std::string owner_name;
  long long id = 0;
  std::string name;
  long long owner_id = 0;
  long long revision = 0;    // optimistic concurrency token
  std::string created_at;
};

struct InstanceRec {
  long long id = 0;
  long long project_id = 0;
  std::string dir;
  std::string input_hash;
  long long uploaded_by = 0;
  std::string created_at;
  std::string label;
};

// A plan version is immutable once written. Editing produces a new version;
// nothing is ever rewritten in place, so an approval can be bound to exact bytes.
struct PlanVersion {
  long long id = 0;
  long long project_id = 0;
  long long instance_id = 0;
  std::string scenario;         // "A" | "B" | "C"
  int version_no = 0;
  long long created_by = 0;
  std::string created_at;
  std::string dir;

  bool feasible = false;
  int violations = 0;
  long long objective_tenths = 0;
  std::string content_hash;     // over the three competition files
  std::string validation_hash;  // over the validation report
  std::string input_hash;       // of the instance it was produced from

  bool is_fallback = false;     // produced with --fallback: breaches scenario policy
  bool strict_buffers = false;

  // draft | approved | superseded | invalidated
  std::string status = "draft";
  long long approved_by = 0;
  std::string approved_at;
};

struct Approval {
  long long id = 0;
  long long plan_version_id = 0;
  long long approved_by = 0;
  std::string approved_at;
  std::string content_hash;
  std::string validation_hash;
  bool revoked = false;
  long long revoked_by = 0;
  std::string revoked_at;
  std::string revoke_reason;
};

struct AuditEvent {
  long long id = 0;
  std::string ts;
  long long actor_id = 0;
  std::string actor_name;
  std::string action;
  std::string object_type;
  std::string object_id;
  std::string result;
  std::string correlation_id;
  std::string detail;
};

class Store {
 public:
  ~Store();
  // Opens (creating if needed) and applies migrations. `err` is set on failure.
  bool Open(const std::string& path, std::string* err);

  // --- users -------------------------------------------------------------
  bool CreateUser(const std::string& username, const std::string& password, Role role,
                  User* out, std::string* err);
  std::optional<User> FindUser(const std::string& username);
  std::optional<User> UserById(long long id);
  std::vector<User> ListUsers();
  bool SetUserDisabled(long long id, bool disabled, std::string* err);
  bool SetUserRole(long long id, Role role, std::string* err);
  bool SetPassword(long long id, const std::string& password, std::string* err);
  // Verifies credentials. Returns nullopt for unknown user, wrong password or a
  // disabled account, without distinguishing them to the caller.
  std::optional<User> Authenticate(const std::string& username, const std::string& password);
  int UserCount();

  // --- sessions ----------------------------------------------------------
  // Returns the bearer token. Only its hash is stored, so a database copy does
  // not yield usable sessions.
  std::string CreateSession(long long user_id, int idle_seconds, int absolute_seconds);
  std::optional<User> UserForSession(const std::string& token);   // also refreshes last_seen
  void RevokeSession(const std::string& token);
  void RevokeAllSessionsFor(long long user_id);
  int PurgeExpiredSessions();

  // --- projects ----------------------------------------------------------
  bool CreateProject(const std::string& name, long long owner_id, Project* out, std::string* err);
  std::optional<Project> ProjectById(long long id);
  std::vector<Project> ListProjects();
  std::vector<Project> ListVisibleProjects(const User& user, long long before, int limit);
  // Optimistic concurrency: fails if `expected_revision` is not current.
  bool BumpProjectRevision(long long id, long long expected_revision, std::string* err);

  // --- instances ---------------------------------------------------------
  bool AddInstance(const InstanceRec& rec, InstanceRec* out, std::string* err);
  std::optional<InstanceRec> InstanceById(long long id);
  std::vector<InstanceRec> ListInstances(long long project_id);

  // --- plan versions -----------------------------------------------------
  bool AddPlanVersion(const PlanVersion& pv, PlanVersion* out, std::string* err);
  std::optional<PlanVersion> PlanVersionById(long long id);
  std::vector<PlanVersion> ListPlanVersions(long long project_id);

  // Approval is bound to the exact content and validation the approver saw. A
  // mismatch means the plan changed underneath them and the call fails.
  bool ApprovePlan(long long version_id, long long approver_id,
                   const std::string& expect_content_hash,
                   const std::string& expect_validation_hash, std::string* err);
  bool RevokeApproval(long long version_id, long long actor_id, const std::string& reason,
                      std::string* err);
  std::vector<Approval> ListApprovals(long long project_id);
  // Marks every approved version of this project as invalidated because the
  // input it was produced from is no longer current. Returns how many.
  int InvalidateApprovalsForChangedInput(long long project_id, const std::string& new_input_hash,
                                         long long actor_id);

  // --- audit -------------------------------------------------------------
  void Audit(long long actor_id, const std::string& action, const std::string& object_type,
             const std::string& object_id, const std::string& result,
             const std::string& correlation_id, const std::string& detail);
  std::vector<AuditEvent> ListAudit(int limit, const std::string& object_type,
                                    const std::string& object_id);

  // --- coordinator assignments ------------------------------------------
  // Scoped to project + instance + activity id. `coordinator_id` of 0 clears
  // the assignment, which is how an activity returns to "Unassigned".
  bool SetAssignment(long long project_id, long long instance_id,
                     const std::string& activity_id, long long coordinator_id,
                     long long assigned_by, std::string* err);
  std::vector<Assignment> ListAssignments(long long project_id, long long instance_id);

  // --- profile photos ----------------------------------------------------
  bool SetUserPhoto(long long user_id, const std::string& media_type,
                    const std::string& bytes, std::string* err);
  bool ClearUserPhoto(long long user_id);
  bool GetUserPhoto(long long user_id, std::string* media_type, std::string* bytes);
  // Cheap existence check, so a list of people can say who has a photo without
  // the client discovering it by requesting each one and collecting 404s.
  bool HasUserPhoto(long long user_id);

  // Audit events belonging to one project, and nothing else.
  //
  // `ListAudit` is deliberately global: a workspace administrator reviewing the
  // whole installation needs account events too. A project view must not use it.
  // An event belongs to a project when it names that project, an instance under
  // it, or a plan version under it. Account-level events (sign-in, user.create,
  // user.role, authz.deny) belong to no project and are never returned here,
  // even to an administrator - the project view is scoped by the object, not by
  // the reader's privilege.
  std::vector<AuditEvent> ListProjectAudit(long long project_id, int limit);

 private:
  bool Exec(const std::string& sql, std::string* err);

  // One connection, shared by every request thread. SQLite's own serialisation
  // cannot be relied on (a build may be compiled multi-thread rather than
  // serialized), and several calls here read connection-global state such as
  // sqlite3_changes and sqlite3_last_insert_rowid immediately after a statement,
  // which is only meaningful if no other thread ran one in between. So every
  // public method takes this lock for its whole duration. SQLite is far faster
  // than the solves this service exists to run; a coarse lock costs nothing here
  // and removes the whole class of problem.
  mutable std::recursive_mutex mu_;
  sqlite3* db_ = nullptr;
};

}  // namespace ta
