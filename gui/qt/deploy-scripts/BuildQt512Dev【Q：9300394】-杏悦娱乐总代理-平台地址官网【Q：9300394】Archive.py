import os
import sys
import fnmatch
import subprocess
import shutil

from util import *

SEVENZIP="C:\\Program Files\\7-Zip\\7z.exe"
QTBASEDIR="C:\\"
QT32 = "Qt\\Qt5.12.0"
QT64 = "Qt\\Qt5.12.0x64"
LIB_DLL_DIR = "5.12\\msvc2015"
ARC_PREFIX = "Qt5120_Rel_"
ARC_SUFFIX_DEV = "_DevDeploy"

# What libraries need to be included?
# You don't have to specify every library necessary to make things run.
# You can simply specify the libraries in your .pro file, and this script
# will do the rest. (It detects subdependencies on its own!)
QT_LIB_INCLUDE = "core gui quick widgets quickwidgets"

def collect_qt_files(deploy_tool, dest, exe_file):
    os.environ.pop("VCINSTALLDIR", None)
    if not simple_exec([deploy_tool, "--qmldir", "qml", "--dir", dest, exe_file]):
        print(" ! ERROR: Failed to collect Qt dependencies!")
        print(" !        See above output for details.")
        sys.exit(1)

print("=====================================================")
print("= Building Qt v5.12 development archive (dynamic)... =")
print("=====================================================")

# Modify PATH if needed
entire_path = os.environ["PATH"]
entire_path = entire_path.split(os.pathsep)
entire_path = [p for p in entire_path if "PyQt4" not in p]
entire_path = os.pathsep.join(entire_path)
os.environ["PATH"] = entire_path

cdir = os.getcwd()

print(" * Stage 0: Removing Old Archives")
silentremove(os.path.join(cdir, ARC_PREFIX + "Win32" + ARC_SUFFIX_DEV + ".7z"))
silentremove(os.path.join(cdir, ARC_PREFIX + "Win64" + ARC_SUFFIX_DEV + ".7z"))

print(" * Stage 1: Building CEmu")
os.chdir(os.path.dirname(os.path.realpath(__file__)))
mkdir_p("build_32")

os.chdir("build_32")
if not simple_exec([r'C:\Qt\Qt5.12.0\5.12\msvc2015\bin\qmake', '-spec', 'win32-msvc2015', '-tp', 'vc', r'..\..\CEmu.pro']):
    print(" ! ERROR: Creating project files for x86 failed!")
    sys.exit(1)
if not simple_exec(["msbuild", r'CEmu.vcxproj', r'/p:Configuration=Release']):
    print(" ! ERROR: Building for x86 failed!")
    sys.exit(1)
os.chdir("..")

print(" * Stage 2: Building Exclusion List")

print("   -> Stage 2a: Collecting Qt Dependencies")
mkdir_p("tmp_devarchive")

cemu32_exe = os.path.join("build_32", "release", "CEmu.exe")

collect_qt_files(r"C:\Qt\Qt5.12.0\5.12\msvc2015\bin\windeployqt.exe", "tmp_devarchive", cemu32_exe)

found_dlls = []

for root, dirnames, filenames in os.walk("tmp_devarchive"):
    for filename in fnmatch.filter(filenames, '*.dll'):
        found_dlls.append(os.path.join(root, filename))

# Get actual DLL filenames (basename)
found_dlls = [p.split(os.sep)[-1].lower() for p in found_dlls]

# Get stuff before file extension
found_dlls = [".".join(p.split(".")[:-1]) for p in found_dlls]

print("   -> Stage 2b: Creating Exclusion List")

# First, find all of the DLL files
dll_matches = []
lib_matches = []
pdb_matches = []

# Is it bad to just base everything off of 32-bit? Assuming libraries
# have the same version, maybe not...

for root, dirnames, filenames in os.walk(os.path.join(QTBASEDIR, QT32, LIB_DLL_DIR)):
    for filename in fnmatch.filter(filenames, 'Qt5*.dll'):
        dll_matches.append(os.path.join(root, filename))

for root, dirnames, filenames in os.walk(os.path.join(QTBASEDIR, QT32, LIB_DLL_DIR)):
    for filename in fnmatch.filter(filenames, '*.lib'):
        lib_matches.append(os.path.join(root, filename))

for root, dirnames, filenames in os.walk(os.path.join(QTBASEDIR, QT32, LIB_DLL_DIR)):
    for filename in fnmatch.filter(filenames, '*.pdb'):
        pdb_matches.append(os.path.join(root, filename))

# Get the actual DLL filenames
# Result: Qt5Gui.dll, Qt5Guid.dll, etc.
# (Note that the debug DLLs are still in there)
dll_matches = [p.split(os.sep)[-1] for p in dll_matches]
lib_matches = [p.split(os.sep)[-1] for p in lib_matches]
pdb_matches = [p.split(os.sep)[-1] for p in pdb_matches]

# Remove debug DLLs from the list
# DLLs that remain, strip off the file extension.
# Result: Qt5Gui, Qt5Help, etc.
dll_matches = [".".join(p.split(".")[:-1]) for p in dll_matches if not p.lower().endswith("d.dll")]
lib_matches = [".".join(p.split(".")[:-1]) for p in lib_matches if not p.lower().endswith("d.lib")]

# We don't filter PDB matches because PDBs are always debug = d.xxx
# That said, let's snip off the "d.pdb" part!
# Do like the above - strip off the file extensions.
# Then strip off the last character = non-debug!
pdb_matches = [".".join(p.split(".")[:-1])[:-1] for p in pdb_matches]

# Filter libraries that are "Info.plist"
lib_matches = [p for p in lib_matches if p != "Info.plist"]

# Build include array!
qt_lib_include_arr = [ ("qt5" + lib.lower()) for lib in QT_LIB_INCLUDE.split(" ") ] + found_dlls

# Remove duplicates...
qt_lib_include_arr = list(set(qt_lib_include_arr))

# Remove the DLLs that are included in the config above, and leave
# the ones to exclude
dll_matches = [p for p in dll_matches if not p.lower() in qt_lib_include_arr]
lib_matches = [p for p in lib_matches if not p.lower() in qt_lib_include_arr]
pdb_matches = [p for p in pdb_matches if not p.lower() in qt_lib_include_arr]

# Filter lib files to exclude DLL matches
lib_matches = [p for p in lib_matches if not p in dll_matches]

# Filter pdb files to exclude DLL *and* LIB matches
pdb_matches = [p for p in pdb_matches if not p in dll_matches]
pdb_matches = [p for p in pdb_matches if not p in lib_matches]

# Combine all matches together!
prefix_matches = dll_matches + lib_matches + pdb_matches

# Ensure that qtmain is NOT excluded!
prefix_matches.remove("qtmain")

# Ensure that XML is NOT excluded!
# Lconvert requires it
prefix_matches.remove("Qt5Xml")
prefix_matches.remove("Qt5XmlPatterns")

# Ensure EGL/libGLESv2 are NOT excludes... but only if we have
# QtQuick/OpenGL involved.
qt_lib_include_arr_quick = [p for p in qt_lib_include_arr if "quick" in p.lower()]
if len(qt_lib_include_arr_quick) > 0:
    if "libEGL" in prefix_matches:
        prefix_matches.remove("libEGL")
    if "libGLESv2" in prefix_matches:
        prefix_matches.remove("libGLESv2")
    if "opengl32sw" in prefix_matches:
        prefix_matches.remove("opengl32sw")
    
    # Do a list comp to remove d3dcompiler_XX
    prefix_matches = [p for p in prefix_matches if not p.lower().startswith("d3dcompiler_")]

# Create exclusion commands!
dll_excludes_nondebug = ["-xr!" + p + ".dll" for p in prefix_matches]
dll_excludes_debug = ["-xr!" + p + "d.dll" for p in prefix_matches]
lib_excludes_nondebug = ["-xr!" + p + ".lib" for p in prefix_matches]
lib_excludes_debug = ["-xr!" + p + "d.lib" for p in prefix_matches]
prl_excludes_nondebug = ["-xr!" + p + ".prl" for p in prefix_matches]
prl_excludes_debug = ["-xr!" + p + "d.prl" for p in prefix_matches]
pdb_excludes = ["-xr!" + p + "d.pdb" for p in prefix_matches]

all_excludes = dll_excludes_nondebug + dll_excludes_debug + \
                lib_excludes_nondebug + lib_excludes_debug + \
                prl_excludes_nondebug + prl_excludes_debug + pdb_excludes

if (not ("webengine" in qt_lib_include_arr)) and (not ("webenginecore" in qt_lib_include_arr)):
    all_excludes.append("-xr!qtwebengine*")

#print(qt_lib_include_arr)
#print(all_excludes)
#print(len(prefix_matches))
#print(len(all_excludes))
#input()

shutil.rmtree("tmp_devarchive")
shutil.rmtree("build_32")

# To preserve path in 7-Zip, you MUST change directory to where the
# folder is. If you don't, the first part of the path will be lost.
os.chdir(QTBASEDIR)

# IMPORTANT NOTE: All paths must be in quotes! Otherwise, 7-Zip will
# NOT parse the paths correctly, and weird things will happen!

print(" * Stage 3: Building Development Archive (x86)")
subprocess.call([SEVENZIP, "a", os.path.join(cdir, ARC_PREFIX + "Win32" + ARC_SUFFIX_DEV + ".7z"), QT32,
                "-t7z", "-m0=lzma2", "-mx9",
                    # Exclude unnecessary files
                    
                    # Installation files
                    "-xr!" + os.path.join(QT32, "network.xml"),
                    "-xr!" + os.path.join(QT32, "Maintenance*"),
                    "-xr!" + os.path.join(QT32, "InstallationLog.txt"),
                    "-xr!" + os.path.join(QT32, "components.xml"),
                    
                    # Tools, examples, documentation, and VCRedist
                    "-xr!" + os.path.join(QT32, "Tools"),
                    "-xr!" + os.path.join(QT32, "Examples"),
                    "-xr!" + os.path.join(QT32, "Docs"),
                    "-xr!" + os.path.join(QT32, "vcredist"),
                    "-xr!" + os.path.join(QT32, LIB_DLL_DIR, "doc"),
                    
                    # Note that we can't just delete all debug DLLs,
                    # LIBs, or PDBs, since they are required to compile
                    # debug builds. We have to find the libraries we
                    # need, then exclude the rest - the technique done
                    # above.
                ] + all_excludes)

print(" * Stage 4: Building Development Archive (x64)")
subprocess.call([SEVENZIP, "a", os.path.join(cdir, ARC_PREFIX + "Win64" + ARC_SUFFIX_DEV + ".7z"), QT64,
                "-t7z", "-m0=lzma2", "-mx9",
                    # Exclude unnecessary files
                    
                    # Installation files
                    "-xr!" + os.path.join(QT64, "network.xml"),
                    "-xr!" + os.path.join(QT64, "Maintenance*"),
                    "-xr!" + os.path.join(QT64, "InstallationLog.txt"),
                    "-xr!" + os.path.join(QT64, "components.xml"),
                    
                    # Tools, examples, documentation, and VCRedist
                    "-xr!" + os.path.join(QT64, "Tools"),
                    "-xr!" + os.path.join(QT64, "Examples"),
                    "-xr!" + os.path.join(QT64, "Docs"),
                    "-xr!" + os.path.join(QT64, "vcredist"),
                    "-xr!" + os.path.join(QT64, LIB_DLL_DIR, "doc"),
                    
                    # Note that we can't just delete all debug DLLs,
                    # LIBs, or PDBs, since they are required to compile
                    # debug builds. We have to find the libraries we
                    # need, then exclude the rest - the technique done
                    # above.
                ] + all_excludes)

print(" * All done!")
