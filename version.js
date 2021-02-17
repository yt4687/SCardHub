if (WScript.Arguments.length == 0)
	WScript.Quit(1);

var in_name = WScript.Arguments(0);
var rc_name = in_name.replace(/in$/, "rc");

var fs = WScript.CreateObject("Scripting.FileSystemObject");
var sh = WScript.CreateObject("WScript.Shell");

function shell(cmd) {
    try {
        var exe = sh.Exec(cmd);
        var str = exe.StdOut.ReadAll();
        return str.length == 0 ? null : str;
    }
    catch (e) {}
    return null;
}

sh.CurrentDirectory = fs.GetFile(in_name).ParentFolder.Path;
var VERSION   = shell("hg log -r . --template {date(date,'%Y,%m,%d,%H%M')}");
var HGVersion = shell("hg log -r . --template r{rev}:{node|short}");

if (VERSION === null) {
	var d = new Date();
	var h = d.getHours();
	var m = ("0" + d.getMinutes()).slice(-2);
	VERSION = [d.getYear(), d.getMonth() + 1, d.getDate(), h + m].join(",");
}
VERSION = VERSION.replace(/,0+/g, ","); /* Zero suppress */

if (HGVersion === null) {
    HGVersion = VERSION.replace(/,/g, ".");
}

function read(path) {
    try {
        var file = fs.OpenTextFile(path);
        return file.ReadAll();
    }
    catch (e) {}
    return "";
}

var template = read(in_name);
template = template.replace(/@VERSION@/g, VERSION);
template = template.replace(/@HGVersion@/g, HGVersion);

var rc = read(rc_name);
if (rc.localeCompare(template) != 0) {
    var file = fs.CreateTextFile(rc_name, true);
    file.Write(template);
    file.Close();
}
