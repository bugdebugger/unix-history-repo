#! /bin/sh -
#	@(#)makesyscalls.sh	8.1 (Berkeley) 6/10/93
# $Id: makesyscalls.sh,v 1.12 1995/03/16 18:12:39 bde Exp $

set -e

# name of compat option:
compat=COMPAT_43

# output files:
sysnames="syscalls.c"
sysproto="../sys/sysproto.h"
sysproto_h=_SYS_SYSPROTO_H_
syshdr="../sys/syscall.h"
syssw="init_sysent.c"
syshide="../sys/syscall-hide.h"

# tmp files:
sysdcl="sysent.dcl"
syscompat="sysent.compat"
sysent="sysent.switch"

trap "rm $sysdcl $syscompat $sysent" 0

case $# in
    0)	echo "Usage: $0 input-file" 1>&2
	exit 1
	;;
esac

awk < $1 "
	BEGIN {
		sysdcl = \"$sysdcl\"
		sysproto = \"$sysproto\"
		sysproto_h = \"$sysproto_h\"
		syscompat = \"$syscompat\"
		sysent = \"$sysent\"
		sysnames = \"$sysnames\"
		syshdr = \"$syshdr\"
		compat = \"$compat\"
		syshide = \"$syshide\"
		infile = \"$1\"
		"'

		printf "/*\n * System call switch table.\n *\n" > sysent
		printf " * DO NOT EDIT-- this file is automatically generated.\n" > sysent

		printf "/*\n * System call prototypes.\n *\n" > sysdcl
		printf " * DO NOT EDIT-- this file is automatically generated.\n" > sysdcl

		printf "\n#ifdef %s\n\n", compat > syscompat

		printf "/*\n * System call names.\n *\n" > sysnames
		printf " * DO NOT EDIT-- this file is automatically generated.\n" > sysnames

		printf "/*\n * System call numbers.\n *\n" > syshdr
		printf " * DO NOT EDIT-- this file is automatically generated.\n" > syshdr
		printf "/*\n * System call hiders.\n *\n" > syshide
		printf " * DO NOT EDIT-- this file is automatically generated.\n" > syshide
	}
	NR == 1 {
		gsub("\\$Id: ", "", $0)
		gsub(" \\$", "", $0)
		printf " * created from%s\n */\n\n", $0 > sysent
		printf "#include <sys/param.h>\n" > sysent
		printf "#include <sys/sysent.h>\n" > sysent
		printf "#include <sys/sysproto.h>\n\n" > sysent
		printf "#ifdef %s\n", compat > sysent
		printf "#define compat(n, name) n, (sy_call_t *)__CONCAT(o,name)\n" > sysent
		printf("#else\n") > sysent
		printf("#define compat(n, name) 0, (sy_call_t *)nosys\n") > sysent
		printf("#endif\n\n") > sysent
		printf("/* The casts are bogus but will do for now. */\n") > sysent
		printf "struct sysent sysent[] = {\n" > sysent

		printf " * created from%s\n */\n\n", $0 > sysdcl
		printf("#ifndef %s\n", sysproto_h) > sysdcl
		printf("#define\t%s\n\n", sysproto_h) > sysdcl
		printf "#include <sys/types.h>\n\n", $0 > sysdcl
		printf "struct proc;\n\n", $0 > sysdcl

		printf " * created from%s\n */\n\n", $0 > sysnames
		printf "char *syscallnames[] = {\n" > sysnames

		printf " * created from%s\n */\n\n", $0 > syshdr

		printf " * created from%s\n */\n\n", $0 > syshide
		next
	}
	NF == 0 || $1 ~ /^;/ {
		next
	}
	$1 ~ /^#[ 	]*if/ {
		print > sysent
		print > sysdcl
		print > syscompat
		print > sysnames
		print > syshide
		savesyscall = syscall
		next
	}
	$1 ~ /^#[ 	]*else/ {
		print > sysent
		print > sysdcl
		print > syscompat
		print > sysnames
		print > syshide
		syscall = savesyscall
		next
	}
	$1 ~ /^#/ {
		print > sysent
		print > sysdcl
		print > syscompat
		print > sysnames
		print > syshide
		next
	}
	syscall != $1 {
		printf "%s: line %d: syscall number out of sync at %d\n", \
		   infile, NR, syscall
		printf "line is:\n"
		print
		exit 1
	}
	{	comment = $5
		if (NF < 8)
			for (i = 6; i <= NF; i++)
				comment = comment " " $i
		if (NF < 6) {
			$6 = $5
			$7 = $5 "_args"
			if ($2 == "COMPAT")
				$7 = "o" $7
			$8 = "int"
		}
		if ($4 != "NOHIDE")
			printf("HIDE_%s(%s)\n", $4, $5) > syshide
	}
	$2 == "STD" || $2 == "NODEF" {
		if (( !nosys || $5 != "nosys" ) && ( !lkmnosys ||
			$5 != "lkmnosys")) {
			printf("struct\t%s;\n", $7) > sysdcl
			if ($5 == "exit")
				printf("__dead ") > sysdcl
			printf("%s\t%s __P((struct proc *, struct %s *, int []));\n", \
			    $8, $5, $7) > sysdcl
		}
		if ($5 == "nosys")
			nosys = 1
		if ($5 == "lkmnosys")
			lkmnosys = 1
		printf("\t{ %d, (sy_call_t *)%s },\t\t\t/* %d = %s */\n", \
		    $3, $5, syscall, $6) > sysent
		printf("\t\"%s\",\t\t\t/* %d = %s */\n", \
		    $6, syscall, $6) > sysnames
		if ($2 == "STD")
			printf("#define\tSYS_%s\t%d\n", \
		    	    $6, syscall) > syshdr
		syscall++
		next
	}
	$2 == "COMPAT" {
		printf("struct\t%s;\n", $7) > syscompat
		printf("%s\to%s __P((struct proc *, struct %s *, int []));\n", \
		    $8, $5, $7) > syscompat
		printf("\t{ compat(%d,%s) },\t\t/* %d = old %s */\n", \
		    $3, $5, syscall, $6) > sysent
		printf("\t\"old.%s\",\t\t/* %d = old %s */\n", \
		    $6, syscall, $6) > sysnames
		printf("\t\t\t\t/* %d is old %s */\n", \
		    syscall, comment) > syshdr
		syscall++
		next
	}
	$2 == "LIBCOMPAT" {
		printf("%s\to%s();\n", $8, $5) > syscompat
		printf("\t{ compat(%d,%s) },\t\t/* %d = old %s */\n", \
		    $3, $5, syscall, $6) > sysent
		printf("\t\"old.%s\",\t\t/* %d = old %s */\n", \
		    $6, syscall, $6) > sysnames
		printf("#define\tSYS_%s\t%d\t/* compatibility; still used by libc */\n", \
		    $6, syscall) > syshdr
		syscall++
		next
	}
	$2 == "OBSOL" {
		printf("\t{ 0, (sy_call_t *)nosys },\t\t\t/* %d = obsolete %s */\n", \
		    syscall, comment) > sysent
		printf("\t\"obs_%s\",\t\t\t/* %d = obsolete %s */\n", \
		    $5, syscall, comment) > sysnames
		printf("\t\t\t\t/* %d is obsolete %s */\n", \
		    syscall, comment) > syshdr
		syscall++
		next
	}
	$2 == "UNIMPL" {
		printf("\t{ 0, (sy_call_t *)nosys },\t\t\t/* %d = %s */\n", \
		    syscall, comment) > sysent
		printf("\t\"#%d\",\t\t\t/* %d = %s */\n", \
		    syscall, syscall, comment) > sysnames
		syscall++
		next
	}
	{
		printf "%s: line %d: unrecognized keyword %s\n", infile, NR, $2
		exit 1
	}
	END {
		printf("\n#endif /* %s */\n", compat) > syscompat
		printf("\n#endif /* !%s */\n", sysproto_h) > syscompat

		printf("};\n\n") > sysent
		printf ("struct sysentvec aout_sysvec = {\n") > sysent
		printf ("\tsizeof (sysent) / sizeof (sysent[0]),\n") > sysent
		printf ("\tsysent,\n") > sysent
		printf ("\t0,\n\t0,\n\t0,\n\t0,\n\t0,\n\t0\n};\n") > sysent
		printf("};\n") > sysnames
	} '

cat $sysent >$syssw
cat $sysdcl $syscompat >$sysproto
