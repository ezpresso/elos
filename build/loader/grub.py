import build.config as config

class Loader():
	def __init__(self, helper, arch):
		self.helper = helper
		self.arch = arch

	def KernPath(self):
		# Put the kernel in the top level boot directory
		return config.Location.BOOT.Path()

	def Build(self):
		env = self.helper.GlobalEnv()
		kern = self.helper.GetTarget("kernel")
		imgdir = self.helper.ImageRootDir()
		bootdir = self.helper.ImageDir(config.Location.BOOT)
		assert kern != None

		env.Install("#" + bootdir + "/boot/grub/", "#build/loader/grub.cfg")
		initrd = self.helper.Initrd("#" + bootdir)

		# Actually build the boot image
		env.Command("#" + imgdir + "boot.iso", [initrd, kern.GetInstall()],
			"grub-mkrescue -o $TARGET " + bootdir)
