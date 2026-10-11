Understand the following projects:

~/development/git/agon-net-utils
~/development/git/trs-net-tcp
~/development/git/trs-os-zds
~/development/git/fujinet-firmware

Put together a plan only in this directory that conforms with the
following paragraphs.

The serial protocol used by ~/development/git/trs-os-zds generated
TRSDOS.bin artifact on the Agon Light 2 to communicate with the
trs-net-tcp server is instead replaced by the protocol for the
fujinet-firmware server.

For the fujinet-firmware server, validate that it supports a direct
serial protocol that we can plan to use for this.

Currently, the openstream/sendstream/closestream utilities from the
agon-net-utils project provide the ability to open a serial stream
via TCP/IP while running Agon MOS, validate that it's correct, and
then close it when we're finished. This link can be established
before using the https://github.com/sijnstra/agon-projects/tree/main/OSboot
utility to launch the TRSDOS.bin artifact for the trs-os-zds project.
From the perspective of TRSDOS/LSDOS the communication link for
remote drives is always via a serial link. Determine if this is
still possible with the fujinet=firmware server. The current
trs-net-tcp server is absolutely silent until the client initiates
activity via the serial protocol. Is this also true of the
fujinet-firmware after a streaming connection has been established?

Identify what the challenges are going to be doing this. I'd like
to create TRSDOS native utilities for this similar to the following
FujiNet utilities for NitrOS-9:

~/development/git/nitros9-main/level1/cmds/fngethost.as
~/development/git/nitros9-main/level1/cmds/fngetdevfile.as
~/development/git/nitros9-main/level1/cmds/fnsethost.as
~/development/git/nitros9-main/level1/cmds/fnlisthosts.as
~/development/git/nitros9-main/level1/cmds/fnstatus.as
~/development/git/nitros9-main/level1/cmds/fnmountimg.as
~/development/git/nitros9-main/level1/cmds/fnsetdevfile.as
~/development/git/nitros9-main/level1/cmds/fnmount.as
~/development/git/nitros9-main/level1/cmds/fnlistdevs.as

The above utilities are written in 6809 assembly code targeting
NitrOS-9 so the utilities for TRSDOS/LSDOS will of course be
implemented differently. Our goal here is the same or very similar
functionality.
