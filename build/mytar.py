from SCons.Script import Action, Builder
import os

def mytar_filter(tar):
	if tar.isreg() and tar.name.endswith(".DS_Store"):
		return None

	print("MyTar: adding " + tar.name)
	return tar

def mytar(target, source, env):
	import tarfile

	# The first "source" item is the folder in which all
	# the source files reside
	rootdir = str(source.pop(0)) + "/"
	pathoff = len(rootdir)

	tar = tarfile.open(str(target[0]), "w", format=tarfile.USTAR_FORMAT)


	tar.add(rootdir, "/",  filter=mytar_filter)

	"""
	The problem: this code does not add the parent directories of files
	(e.g. bin/ for bint/init) and the kernel is dumb and assumes that the
	parent directories are before the actual files in the tar archive....

	tar.add(rootdir + "bin", "bin",  recursive=False)
	for item in source:
		name = str(item)
		assert(name.startswith(rootdir))

		# The name of the files are relative to the top level
		# project directory, which is problematic for the initrd.
		# Thus remove the first part of e.g.
		# "bin/$(TARGET)/image/initrd/bin/init" to obtain "bin/init".
		name = name[pathoff:]

		print("MyTar: adding " + name)
		tar.add(str(item), name)
	"""

	tar.close()

def mytarstr(target, source, env):
	return 'MyTar(%s,...)' % target[0]

def builder():
	action = Action(mytar, mytarstr)
	return Builder(action=action)
