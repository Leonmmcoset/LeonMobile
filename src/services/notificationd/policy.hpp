#pragma once
#include "notificationd.h"
#include <leon/package.hpp>
namespace leon::notifications {
void load(notification_store &, const notification_options &);
void unload(notification_store &);
leon::Json policy(notification_store &, const notification_options &, uid_t caller,
                  const leon::Json &);
bool enabled(const notification_store &, const std::string &app);
void alert(notification_store &, const std::string &app);
void tick(notification_store &, const notification_options &);
std::string lockscreen(const notification_store &);
bool locked(const notification_options &, std::string &epoch);
} // namespace leon::notifications
