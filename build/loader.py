from .util import execfile

def setup(helper):
	conf = helper.GlobalConf()
	execfile("build/loader/" + conf.GetConfig('LOADER') + ".py", globals())
	return Loader(helper, conf.GetConfig('LOADER_ARCH'))
