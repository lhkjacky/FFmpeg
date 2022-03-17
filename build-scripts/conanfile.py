from conans import ConanFile, tools

# NOTE: when updating, place libraries in alphabetical order to reduce diffs

class conanRecipe(ConanFile):
    name = "FFmpeg"
    #version
    settings = ("os", "build_type", "arch")

    def configure(self):
        pass


    def requirements(self):
        self.requires("videoai/0.3.0")

    def imports(self):
        if self.settings.os == "Windows":
            self.copy("*", "lib3rdparty", folder=True)
            self.copy("*", "bin", "bin")
            self.copy("*", "bin", "binr")
            # self.copy('*', src='@bindirs', dst='binr')
        if self.settings.os == "Macos":
            self.copy("*", "include", "include")
            self.copy("*", "lib", "lib")
