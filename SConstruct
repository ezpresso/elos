import build.mytar as mytar
import build.config as config
import build.loader as loader
from build.util import SkipBody
import os

# The root filesystem structure
BINDIR = "/System/bin"
BOOTDIR = "/System/boot"
SERVICEDIR = "/System/service"
MODULEDIR = "/System/module"
SRCDIR = "/System/src"
DEVDIR = "/dev"
APPDIR = "/Applications"
USERDIR = "/Users"
# The boot / initrd filesystem structure
BOOTMODDIR = "/module"
BOOTBINDIR = "/bin"

class Target:
	def __init__(self, name, helper, config, env):
		self.name = name
		self.objects = []
		self.helper = helper
		self.config = config
		self.env = env
		self.output = None
		self.install = None
		self.deps = []

	def __enter__(self):
		return self

	def __exit__(self, type, value, traceback):
		return value is None

	def EnvAppend(self, **kwargs):
		self.env.Append(**kwargs)

	def EnvReplace(self, **kwargs):
		self.env.Replace(**kwargs)

	def GetConfig(self, key):
		try:
		 	return self.config.GetConfig(key)
		except:
			return self.helper.GlobalConf().GetConfig(key)

	def Configured(self, key):
		conf = self.GetConfig(key)
		assert conf != None
		if conf == "CONFIG_YES":
			return True
		else:
			assert conf == "CONFIG_NO"
			return False

	def IncludeDir(self, dir):
		self.EnvAppend(CPPPATH = [ "#" + Dir(dir).srcnode().path])

	def CFlag(self, flags):
		self.EnvAppend(CFLAGS = flags)

	def ASFlag(self, flags):
		self.EnvAppend(ASFLAGS = flags)

	def LinkFlag(self, flags):
		self.EnvAppend(LINKFLAGS = flags)

	def Object(self, *args, **kwargs):
		object = self.env.StaticObject(*args, **kwargs)
		self.objects.extend(object)

	def Program(self):
		self.output = self.env.Program(self.name, self.objects)
		for dep in self.deps:
			Depends(self.output, dep)

	def GetLocation(self):
		return self.config.location

	def GetType(self):
		return self.config.type

	def GetOutput(self):
		assert self.output != None
		return self.output

	def GetInstall(self):
		assert self.install != None
		return self.install

	def Install(self):
		# Copy the binary into the image directory
		dest = self.helper.DestinationDir(self.GetLocation(), self.GetType())
		print("installing " + self.name + " to " + dest)
		self.install = self.env.Install("#" + dest, source=self.output)

	def Depends(self, dep):
		self.deps.append(dep)


class BuildHelper():
	def __init__(self, target, config):
		# Initialize the gobal environment
		env = Environment(
			ENV = { 'PATH': os.environ['PATH'] },
			CCFLAGS = ["-Wall", "-Wextra", "-Wvla", "-Werror"],
			CFLAGS = ["-Wstrict-prototypes"])

		self.globconf = config['global']
		self.globenv = env
		self.config = config
		self.builddir = "bin/" + target + "/"
		self.progs = []

		prefix = self.globconf.GetConfig("TOOLCHAIN")
		env.Replace(CC = prefix + "-gcc")
		env.Replace(CXX = prefix + "-g++")
		env.Replace(LD = prefix + "-gcc")
		env.Replace(AR = prefix + "-ar")
		env.Replace(AS = prefix + "-gcc")
		env.Replace(STRIP = prefix + "-strip")

		# SCons provides a way for building tar archives, however
		# we there is a slight issue with the scons tar and
		# initrd generation.
		build = mytar.builder()
		env.Append(BUILDERS = {'MyTar' : build})

		# Setup the bootloader build stuff
		self.loader = loader.setup(self)

	def GlobalEnv(self):
		return self.globenv

	def GlobalConf(self):
		return self.globconf

	def Target(self, name, _class=Target):
		if name not in self.config:
			target = SkipBody(name)
		else:
			target = _class(name, self, self.config[name], self.globenv.Clone())
			self.progs.append(target)
		return target

	def GetTarget(self, name):
		for t in self.progs:
			if t.name == name:
				return t
		return None

	def Initrd(self, destdir):
		initrd = self.ImageDir(config.Location.INITRD)

		# TODO this is ugly but it works for now...
		if os.path.isdir(initrd) == False:
			Execute(Copy(initrd, "build/initrd"))

		files = [Dir("#" + initrd)]
		for prog in self.progs:
			if prog.GetLocation() == config.Location.INITRD:
				files.extend(prog.GetInstall())

		initrd = destdir + "/initrd.tar"
		return self.globenv.MyTar(initrd, files)

	def ImageRootDir(self):
		return self.builddir + "image/"

	def DestinationDir(self, location, type):
		imagedir = self.ImageRootDir()
		locdir = imagedir + location.Path()

		if type == config.ProgType.NONE:
			return locdir
		elif type == config.ProgType.KERNEL:
			return imagedir + self.loader.KernPath()
		elif type == config.ProgType.SERVICE:
			assert location == config.Location.ROOT
			return locdir + SERVICEDIR
		elif type == config.ProgType.EXE:
			if (location == config.Location.INITRD or
				location == config.Location.BOOT):
				return locdir + BOOTBINDIR
			else:
				return locdir + MODULEDIR
		Error("TODO module")

	def ImageDir(self, location):
		return self.DestinationDir(location, config.ProgType.NONE)

	def BuildDir(self):
		return self.builddir

	def GlobalInclude(self, dir):
		self.globenv.Append(CPPPATH = [ "#" + Dir(dir).srcnode().path])

def Error(msg):
	print("error: " + msg)
	Exit(2)

def main():
	# Parse the command line arguments
	target = ARGUMENTS.get('target')
	if target == None:
		Error("no target specified")

	buildmode = ARGUMENTS.get('mode')
	if buildmode == None:
		Error("no mode specified")
	elif buildmode != "development" and buildmode != "release":
		Error("invalid mode: " + mode)

	conf = config.setup(target, buildmode)
	build = BuildHelper(target, conf)

	Export('build Error SkipBody Target')
	SConscript(dirs="src", variant_dir=build.BuildDir(), duplicate=0)

	build.loader.Build()

main()
