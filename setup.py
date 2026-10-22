from setuptools import setup
from wheel.bdist_wheel import bdist_wheel

class LinuxAArch64Wheel(bdist_wheel):
    def finalize_options(self):
        super().finalize_options()
        self.root_is_pure = False

    def get_tag(self):
        return ("py3", "none", "linux_aarch64")

setup(cmdclass={"bdist_wheel": LinuxAArch64Wheel})
