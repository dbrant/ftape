savedcmd_/home/unknown/ftape/ftape-bpck.mod := printf '%s\n'   ftape/parport/bpck-fdc.o | awk '!x[$$0]++ { print("/home/unknown/ftape/"$$0) }' > /home/unknown/ftape/ftape-bpck.mod
