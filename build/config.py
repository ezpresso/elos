from enum import Enum
from .util import execfile

_config = {}
_globalconf = None
_curconf = None

class ProgType(Enum):
	NONE = 0,
	KERNEL = 1
	SERVICE = 2
	EXE = 3
	KERNMOD = 4

class Location(Enum):
	NONE = 0
	BOOT = "boot"
	INITRD = "initrd"
	ROOT = "root"

	def Path(self):
		return self.value

class Configuration():
	def __init__(self, name, type, location):
		self.config = {}
		self.name = name
		self.type = type
		self.location = location

	def __enter__(self):
		global _curconf
		assert _curconf == _globalconf
		_curconf = self
		return self

	def __exit__(self, type, value, traceback):
		global _curconf
		_curconf = _globalconf

	def Config(self, key, val, export):
		self.config[key] = val

	def GetConfig(self, key):
		return self.config[key]

	def Config(self, key, val, export):
		self.config[key] = val

def AddTarget(name, type, location=Location.ROOT):
	assert name not in _config

	conf = Configuration(name, type, location)
	_config[name] = conf
	return conf

def Config(key, val, export=False):
	assert _curconf != None
	_curconf.Config(key, val, export)

def Toolchain(prefix):
	assert _curconf == _globalconf
	Config("TOOLCHAIN", prefix)

def Architecture(arch, archdir=None):
	assert _curconf == _globalconf
	if archdir == None:
		archdir = arch

	Config("ARCH", arch, export=True)
	Config("ARCHDIR", archdir)

def Loader(loader, arch=None):
	assert _curconf == _globalconf
	if arch == None:
		arch = _globalconf.GetConfig("ARCH")

	Config("LOADER", loader)
	Config("LOADER_ARCH", arch)

def setup(target, buildmode):
	global _globalconf, _curconf

	_globalconf = AddTarget("global", ProgType.NONE, Location.NONE)
	if buildmode == "development":
		_globalconf.Config("MODE", "DEVELOPMENT", export=True)
	else:
		assert buildmode == "release"
		_globalconf.Config("MODE", "RELEASE", export=True)

	# Execute the target specific configuration file
	_curconf = _globalconf
	execfile("build/target/%s.py" % target, globals())

	# Return the complete configuration list
	return _config
