savedcmd_/home/unknown/ftape/ftape-parport.mod := printf '%s\n'   ftape/parport/fdc-parport.o | awk '!x[$$0]++ { print("/home/unknown/ftape/"$$0) }' > /home/unknown/ftape/ftape-parport.mod
