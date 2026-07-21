#!/bin/awk -f

BEGIN {
	shared=0; version=0; name="";
}
/^($)/ {
	shared=0; version=0; name="";
	next;
}
/^private/ {
	shared=0; version=0; name="";
	next;
}
/^Dynamic Section:$/ {
	shared=1;
	next;
}
/^Version References:$/ {
	version=1;
	next;
}

(version==1) && /^ *required from/ {
	sub(/:/, "", $3);
	name=$3;
	next;
}
(shared==1) && /^ *NEEDED/ {
	lib[$2]="";
	next;
}
(version==1) && (name!="") && ($4!="") {
	if (lib[name]!="")
		lib[name]=lib[name] ":";
	lib[name]=lib[name] $4;
	next;
}
END {
	for (name in lib)
		print name ":" lib[name];
}
