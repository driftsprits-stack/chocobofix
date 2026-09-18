// Corroboration gate: the upstream pack ships 03_submission_sample/ as "a
// feasible, 0-hard-violation submission against 01_data/". If our independent
// checker calls it infeasible, our rule reconstruction is wrong, not the sample.
#include <iostream>

#include "core/instance.h"
#include "core/schedule.h"
#include "validator/validator.h"

int main(int argc, char** argv) {
  const std::string data = argc > 1 ? argv[1] : "data/upstream/PS1/01_data";
  const std::string sub = argc > 2 ? argv[2] : "data/upstream/PS1/03_submission_sample";

  ta::Instance inst;
  std::vector<ta::InputError> errors;
  if (!ta::LoadInstance(data, &inst, &errors)) {
    std::cerr << "instance failed to load:\n";
    for (const auto& e : errors) std::cerr << "  " << e.Format() << "\n";
    return 1;
  }
  std::cout << "instance: " << inst.activities.size() << " activities, "
            << inst.contracts.size() << " contracts, " << inst.locations.size()
            << " locations, " << inst.horizon_weeks << " weeks\n"
            << "input_hash: " << inst.input_hash << "\n"
            << "never-same-week pairs, adopted reading: " << inst.exclusive_pairs.size() << "\n"
            << "never-same-week pairs, literal reading: " << inst.exclusive_pairs_strict.size() << "\n";

  ta::Plan plan;
  errors.clear();
  if (!ta::LoadPlan(inst, sub, &plan, &errors)) {
    std::cerr << "sample plan failed to parse:\n";
    for (const auto& e : errors) std::cerr << "  " << e.Format() << "\n";
    return 1;
  }
  const auto rep = ta::Validate(inst, plan);
  std::cout << rep.ToJson(inst) << "\n";
  if (!rep.feasible) {
    std::cerr << "\nFAIL: our checker rejects the official sample -> our rules are wrong\n";
    for (size_t i = 0; i < rep.hard_violations.size() && i < 20; ++i)
      std::cerr << "  [" << rep.hard_violations[i].rule << "] " << rep.hard_violations[i].detail << "\n";
    std::cerr << "  (" << rep.hard_violations.size() << " total)\n";
    return 1;
  }
  std::cout << "\nSAMPLE ACCEPTED: 0 hard violations\n";
  return 0;
}
