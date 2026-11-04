# Developer root access

The reference image locks the root account for conventional password login.
Buildroot root login is disabled explicitly: setting the password string to `!`
would hash that literal string rather than lock the account. Image finalization
also locks a previously generated root entry when rebuilding an existing tree.
The developer serial console continues to log in as the non-root `leon` account.
PID 1 and privileged system services retain the UID 0 privileges needed to boot
and manage the system.

The image does not enable the sudo package. Its `sudo` and `sudoedit` policy
entry points always reject execution with status 126, including when invoked
by UID 0. BusyBox `su` and `sulogin` are excluded to prevent alternate built-in
password entry points. User elevation will be mediated by the dedicated root
service, with explicit opt-in and a user-created password.

The policy applies to the shipped system. A user deliberately given unrestricted
UID 0 can modify its implementation; Linux cannot prevent unrestricted root
from replacing the policy with their own code.

The default-policy step passed its image-finalization fixture, an offline check
of the generated ext4 root entry, and the complete QEMU suite with all three
probes and the SDK package. The target checks execute both sudo rejection paths
and verify that BusyBox exposes neither su nor sulogin. Evidence remains ignored
in `build/root-policy-*.log` and `build/qemu-test/`.
