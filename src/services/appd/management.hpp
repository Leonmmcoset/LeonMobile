#pragma once
#include "processes.hpp"
namespace leon::appd {
Json managed_list(Packages &packages, const Processes &processes);
Json managed_info(Packages &packages, const Processes &processes, const Options &options,
                  const std::string &id);
void manage_application(Packages &packages, Processes &processes, const Options &options,
                        const std::string &op, const std::string &id, bool purge = true);
} // namespace leon::appd
