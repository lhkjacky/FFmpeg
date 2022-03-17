from conans import ConanFile, tools

class conanRecipe(ConanFile):
	name = "ffmpeg"
	settings = ("os", "build_type", "arch")

	def requirements(self):
		self.requires("videoai/0.3.0")

	def imports(self):
		if self.settings.os == "Windows":
			self.copy("*")
		if self.settings.os == "Macos":
			self.copy("*")
