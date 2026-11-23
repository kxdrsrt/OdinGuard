// OdinGuard checks of the login-server: guard required, ban by hardware id, client registry.
#ifndef OG_LOGIN_HPP
#define OG_LOGIN_HPP

#include <common/cbasetypes.hpp>

void og_login_init();
void og_login_final();
/// @return 0 when the login may continue, otherwise the refuse code for the client (4 = blocked, 5 = outdated client)
int32 og_login_check(int32 fd, uint32 account_id, const char* userid);

#endif /* OG_LOGIN_HPP */
