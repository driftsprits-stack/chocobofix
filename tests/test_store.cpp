// Behavioural tests for the shared-project layer.
//
// The emphasis is on the rules that protect a plan from being trusted when it
// should not be: who may do what, what an approval is bound to, and what stops
// two people racing each other.
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <thread>

#include "service/crypto.h"
#include "service/store.h"

namespace fs = std::filesystem;
using namespace ta;

namespace {
int g_pass = 0, g_fail = 0;
std::string g_group;
void Group(const char* g) { g_group = g; }
void Check(bool ok, const std::string& what) {
  if (ok) ++g_pass;
  else { ++g_fail; std::cout << "  FAIL [" << g_group << "] " << what << "\n"; }
}
template <typename A, typename B>
void Eq(const A& a, const B& b, const std::string& what) {
  if (a == b) { ++g_pass; return; }
  ++g_fail;
  std::cout << "  FAIL [" << g_group << "] " << what << " (got " << a << ", expected " << b << ")\n";
}
}  // namespace

int main() {
  const std::string db = (fs::temp_directory_path() / "ta_store_test.sqlite").string();
  std::error_code ec;
  fs::remove(db, ec);
  fs::remove(db + "-wal", ec);
  fs::remove(db + "-shm", ec);

  Store s;
  std::string err;
  Check(s.Open(db, &err), "the store opens and migrates: " + err);

  // ---------------------------------------------------------------- passwords
  Group("passwords");
  {
    const std::string h = HashPassword("correct horse battery staple");
    Check(h.rfind("pbkdf2_sha256$", 0) == 0, "the stored form names the algorithm and cost");
    Check(h.find("correct horse") == std::string::npos, "the password does not appear in the hash");
    Check(VerifyPassword("correct horse battery staple", h), "the right password verifies");
    Check(!VerifyPassword("Correct horse battery staple", h), "a one-character change does not");
    Check(!VerifyPassword("", h), "an empty password does not");
    const std::string h2 = HashPassword("correct horse battery staple");
    Check(h != h2, "the same password hashes differently each time (per-user salt)");
    Check(!VerifyPassword("x", "not-a-valid-record"), "a malformed record verifies nothing");
    Check(!VerifyPassword("x", ""), "an empty record verifies nothing");
  }

  // ---------------------------------------------------------------- accounts
  Group("accounts");
  User admin, planner, approver, viewer;
  Check(s.CreateUser("admin.ada", "administrator-pass-1", Role::kAdministrator, &admin, &err),
        "an administrator can be created: " + err);
  Check(s.CreateUser("plan.pat", "planner-password-01", Role::kPlanner, &planner, &err),
        "a planner can be created: " + err);
  Check(s.CreateUser("appr.avi", "approver-password-1", Role::kApprover, &approver, &err),
        "an approver can be created: " + err);
  Check(s.CreateUser("view.vic", "viewer-password-001", Role::kViewer, &viewer, &err),
        "a viewer can be created: " + err);
  Check(!s.CreateUser("plan.pat", "another-password-12", Role::kPlanner, nullptr, &err),
        "a duplicate username is refused");
  Check(!s.CreateUser("bad user", "another-password-12", Role::kPlanner, nullptr, &err),
        "a username with a space is refused");
  Check(!s.CreateUser("shorty", "short", Role::kPlanner, nullptr, &err),
        "a password under 12 characters is refused");
  Eq(s.UserCount(), 4, "four accounts exist");

  Check(s.Authenticate("plan.pat", "planner-password-01").has_value(), "correct credentials sign in");
  Check(!s.Authenticate("plan.pat", "wrong").has_value(), "wrong credentials do not");
  Check(!s.Authenticate("nobody.here", "planner-password-01").has_value(),
        "an unknown account does not sign in");

  // ------------------------------------------------- the permission matrix
  // The two deliberate gaps are the point of this group: an administrator must
  // not be able to approve, and an approver must not be able to create work.
  Group("permission matrix");
  Check(RoleHas(Role::kPlanner, Cap::kRunSolve), "a planner may run a solve");
  Check(!RoleHas(Role::kViewer, Cap::kRunSolve), "a viewer may not");
  Check(!RoleHas(Role::kApprover, Cap::kRunSolve), "an approver may not create work");
  Check(RoleHas(Role::kApprover, Cap::kApprovePlan), "an approver may approve");
  Check(!RoleHas(Role::kAdministrator, Cap::kApprovePlan),
        "an administrator may NOT approve - administration does not imply approval");
  Check(!RoleHas(Role::kPlanner, Cap::kApprovePlan), "a planner may not approve their own work");
  Check(RoleHas(Role::kAdministrator, Cap::kManageUsers), "an administrator may manage accounts");
  Check(!RoleHas(Role::kApprover, Cap::kManageUsers), "an approver may not");
  Check(!RoleHas(Role::kPlanner, Cap::kManageUsers), "a planner may not");
  for (Role r : {Role::kViewer, Role::kPlanner, Role::kApprover, Role::kAdministrator})
    Check(RoleHas(r, Cap::kViewProject), "every signed-in role may view a project");

  // ---------------------------------------------------------------- sessions
  Group("sessions");
  {
    const std::string tok = s.CreateSession(planner.id, 1800, 28800);
    Check(!tok.empty(), "a session token is issued");
    auto u = s.UserForSession(tok);
    Check(u && u->id == planner.id, "the token resolves to its user");
    Check(!s.UserForSession("not-a-real-token").has_value(), "a bogus token resolves to nobody");
    Check(!s.UserForSession("").has_value(), "an empty token resolves to nobody");
    s.RevokeSession(tok);
    Check(!s.UserForSession(tok).has_value(), "a revoked token stops working");

    // Expiry is enforced on read, not by a sweeper, so a stale token is dead the
    // moment it is next presented.
    const std::string expired = s.CreateSession(planner.id, -1, -1);
    Check(!s.UserForSession(expired).has_value(), "an expired session is refused");

    const std::string t2 = s.CreateSession(viewer.id, 1800, 28800);
    Check(s.UserForSession(t2).has_value(), "a viewer session works");
    Check(s.SetUserDisabled(viewer.id, true, &err), "an account can be disabled");
    Check(!s.UserForSession(t2).has_value(), "disabling an account kills its live sessions");
    Check(!s.Authenticate("view.vic", "viewer-password-001").has_value(),
          "a disabled account cannot sign in");
    Check(s.SetUserDisabled(viewer.id, false, &err), "and can be re-enabled");

    // The idle window is the configured one, and the absolute expiry caps it:
    // an active session cannot extend itself past its hard limit.
    {
      const std::string brief_tok = s.CreateSession(planner.id, 5, 10);
      Check(s.UserForSession(brief_tok).has_value(), "a short-window session works immediately");
      Check(s.UserForSession(brief_tok).has_value(), "and keeps working while it is being used");
      s.RevokeSession(brief_tok);
    }
    {
      // Absolute expiry already passed, idle window generous: still refused.
      const std::string stale = s.CreateSession(planner.id, 3600, -1);
      Check(!s.UserForSession(stale).has_value(),
            "the absolute expiry is enforced even when the idle window is wide open");
    }

    const std::string t3 = s.CreateSession(planner.id, 1800, 28800);
    Check(s.SetUserRole(planner.id, Role::kPlanner, &err), "a role can be set");
    Check(!s.UserForSession(t3).has_value(),
          "changing a role ends existing sessions, so capabilities cannot go stale");
  }

  // ------------------------------------------------- projects and concurrency
  Group("optimistic concurrency");
  Project proj;
  Check(s.CreateProject("Northern renewals", planner.id, &proj, &err), "a project is created: " + err);
  Eq(proj.revision, 1LL, "a new project starts at revision 1");
  {
    // Two planners each loaded revision 1. The first write wins; the second is
    // told what happened rather than silently overwriting it.
    Check(s.BumpProjectRevision(proj.id, 1, &err), "the first writer at revision 1 succeeds");
    Check(!s.BumpProjectRevision(proj.id, 1, &err), "the second writer at revision 1 is rejected");
    Check(err.find("revision") != std::string::npos,
          "  ... and the message says which revision they had and which is current");
    Check(s.BumpProjectRevision(proj.id, 2, &err), "reloading and retrying at revision 2 succeeds");
  }

  // ---------------------------------------------------------------- versions
  Group("plan versions");
  InstanceRec inst;
  inst.project_id = proj.id;
  inst.dir = "/tmp/ta_inst_a";
  inst.input_hash = "input-hash-AAA";
  inst.uploaded_by = planner.id;
  Check(s.AddInstance(inst, &inst, &err), "an instance is recorded: " + err);

  auto make_version = [&](bool feasible, int violations, bool fallback, const char* scen) {
    PlanVersion pv;
    pv.project_id = proj.id;
    pv.instance_id = inst.id;
    pv.scenario = scen;
    pv.created_by = planner.id;
    pv.dir = "/tmp/ta_plan";
    pv.feasible = feasible;
    pv.violations = violations;
    pv.objective_tenths = 322;
    pv.content_hash = std::string("content-") + scen + (feasible ? "-ok" : "-bad") + (fallback ? "-fb" : "");
    pv.validation_hash = std::string("validation-") + scen + (feasible ? "-ok" : "-bad");
    pv.input_hash = inst.input_hash;
    pv.is_fallback = fallback;
    PlanVersion out;
    Check(s.AddPlanVersion(pv, &out, &err), std::string("a plan version is recorded: ") + err);
    return out;
  };

  PlanVersion good = make_version(true, 0, false, "A");
  PlanVersion bad = make_version(false, 13, false, "B");
  PlanVersion fb = make_version(true, 0, true, "C");
  Eq(good.version_no, 1, "the first version of scenario A is version 1");
  Eq(good.status, std::string("draft"), "a new version starts as a draft, not approved");
  PlanVersion good2 = make_version(true, 0, false, "A");
  Eq(good2.version_no, 2, "the next version of scenario A is version 2");

  // ------------------------------------------- what may and may not be approved
  Group("approval");
  Check(!s.ApprovePlan(bad.id, approver.id, bad.content_hash, bad.validation_hash, &err),
        "a plan with hard violations cannot be approved");
  Check(err.find("violation") != std::string::npos, "  ... and the refusal says why");
  Check(!s.ApprovePlan(fb.id, approver.id, fb.content_hash, fb.validation_hash, &err),
        "a fallback plan cannot be approved, even though it has no hard violations");
  Check(err.find("fallback") != std::string::npos, "  ... and the refusal names fallback mode");

  // Approval is bound to what the approver was shown.
  Check(!s.ApprovePlan(good.id, approver.id, "some-other-content-hash", good.validation_hash, &err),
        "approval fails if the content hash does not match what was displayed");
  Check(!s.ApprovePlan(good.id, approver.id, good.content_hash, "some-other-validation", &err),
        "approval fails if the validation hash does not match what was displayed");
  Check(s.ApprovePlan(good.id, approver.id, good.content_hash, good.validation_hash, &err),
        "approval succeeds when both hashes match: " + err);
  {
    auto now = s.PlanVersionById(good.id);
    Check(now && now->status == "approved", "the version is marked approved");
    Check(now && now->approved_by == approver.id, "and records who approved it");
    Check(!now->approved_at.empty(), "and when");
  }
  Check(!s.ApprovePlan(good.id, approver.id, good.content_hash, good.validation_hash, &err),
        "the same version cannot be approved twice");

  // Approving a newer version supersedes the older one, so only one plan per
  // scenario ever stands approved.
  Check(s.ApprovePlan(good2.id, approver.id, good2.content_hash, good2.validation_hash, &err),
        "a newer version can be approved: " + err);
  {
    auto old_v = s.PlanVersionById(good.id);
    Check(old_v && old_v->status == "superseded", "the earlier approved version becomes superseded");
  }

  Group("revocation and invalidation");
  Check(s.RevokeApproval(good2.id, approver.id, "found a better plan", &err),
        "an approval can be revoked: " + err);
  {
    auto v = s.PlanVersionById(good2.id);
    Check(v && v->status == "draft", "the version returns to draft");
    auto apps = s.ListApprovals(proj.id);
    bool found = false;
    for (const auto& a : apps)
      if (a.plan_version_id == good2.id && a.revoked && a.revoke_reason == "found a better plan")
        found = true;
    Check(found, "the revocation is recorded with its reason, and the history is kept");
  }
  {
    // Re-approve, then replace the input the plan was built from.
    Check(s.ApprovePlan(good2.id, approver.id, good2.content_hash, good2.validation_hash, &err),
          "re-approval after revocation works: " + err);
    const int n = s.InvalidateApprovalsForChangedInput(proj.id, "input-hash-BBB", planner.id);
    Check(n > 0, "replacing the input invalidates plans built on the old one");
    auto v = s.PlanVersionById(good2.id);
    Check(v && v->status == "invalidated", "the approved version is now marked invalidated");
    Check(!s.ApprovePlan(good2.id, approver.id, good2.content_hash, good2.validation_hash, &err),
          "an invalidated version cannot simply be re-approved");
    Check(err.find("no longer current") != std::string::npos,
          "  ... and the refusal explains that the input changed");
  }

  // ---------------------------------------------------------------- audit
  Group("audit");
  {
    s.Audit(approver.id, "plan.approve", "plan_version", std::to_string(good.id), "ok", "corr-1",
            "scenario A");
    s.Audit(planner.id, "plan.create", "plan_version", std::to_string(good.id), "ok", "corr-2", "");
    auto ev = s.ListAudit(50, "plan_version", std::to_string(good.id));
    Eq(ev.size(), size_t(2), "both events are recorded against the object");
    Check(ev[0].actor_name == "plan.pat" || ev[0].actor_name == "appr.avi",
          "the event resolves the actor's name");
    Check(!ev[0].ts.empty(), "the event carries a timestamp");
    Check(!ev[0].correlation_id.empty(), "and a correlation id");
    Check(!ev[0].event_hash.empty(), "the event is included in the audit hash chain");
    Check(s.VerifyAuditChain(&err), "the audit hash chain verifies");
    auto none = s.ListAudit(50, "plan_version", "999999");
    Eq(none.size(), size_t(0), "an object with no events returns none");
  }

  Group("project pagination");
  {
    Project extra;
    Check(s.CreateProject("Second project", planner.id, &extra, &err), "second project created");
    auto first = s.ListVisibleProjects(planner, 0, 1);
    Eq(first.size(), size_t(1), "page is bounded");
    Eq(first[0].id, extra.id, "newest project first");
    Eq(first[0].owner_name, planner.username, "owner comes from joined query");
    auto next = s.ListVisibleProjects(planner, first[0].id, 1);
    Eq(next.size(), size_t(1), "cursor reaches older project");
    Eq(next[0].id, proj.id, "cursor does not repeat boundary row");
    User outsider; outsider.id = 987654; outsider.role = Role::kPlanner;
    Check(s.ListVisibleProjects(outsider, 0, 100).empty(), "other planner cannot list projects");
    Check(s.ListVisibleProjects(approver, 0, 100).size() >= 2, "approver can review both projects");
  }

  // ---------------------------------------------------------------- tokens
  Group("tokens");
  {
    Check(RandomToken(32).size() == 64, "a token is 32 bytes of hex");
    Check(RandomToken() != RandomToken(), "tokens differ between calls");
    Check(ConstantTimeEquals("abc", "abc"), "equal strings compare equal");
    Check(!ConstantTimeEquals("abc", "abd"), "different strings do not");
    Check(!ConstantTimeEquals("abc", "abcd"), "different lengths do not");
  }

  std::cout << "\n" << g_pass << " passed, " << g_fail << " failed\n";
  fs::remove(db, ec);
  fs::remove(db + "-wal", ec);
  fs::remove(db + "-shm", ec);
  return g_fail == 0 ? 0 : 1;
}
