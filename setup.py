import os
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext

this_dir = os.path.abspath(os.path.dirname(__file__))


class BuildExt(build_ext):
    def build_extensions(self):
        compiler_type = self.compiler.compiler_type
        for ext in self.extensions:
            if compiler_type == "msvc":
                # MSVC flags adjusted for aggressive performance optimization
                ext.extra_compile_args = [
                    "/Ox",
                    "/std:c++20",
                    "/GL",
                    "/fp:fast",
                    "/arch:AVX2",
                ]
                ext.extra_link_args = ["/LTCG"]
            else:
                # GCC/Clang flags (Linux/macOS)
                ext.extra_compile_args = ["-O3", "-Wall", "-std=c++20", "-flto"]
                ext.extra_link_args = ["-flto"]
        super().build_extensions()


module = Extension(
    "vldt._vldt",
    sources=[
        "src/vldt_module.cpp",
        "src/data_model.cpp",
        "src/init_globals.cpp",
        "src/conversion/dict_utils.cpp",
        "src/conversion/json_utils.cpp",
        "src/conversion/rapidjson_to_pyobject.cpp",
        "src/schema/schema.cpp",
        "src/schema/deserializer.cpp",
        "src/validation/validation.cpp",
        "src/validation/validation_containers.cpp",
        "src/validation/validation_primitives.cpp",
        "src/validation/validation_validators.cpp",
    ],
    include_dirs=[
        os.path.join(this_dir, "external/rapidjson/include"),
        os.path.join(this_dir, "src"),
        os.path.join(this_dir, "src/conversion"),
        os.path.join(this_dir, "src/validation"),
    ],
    language="c++",
)

setup(
    name="vldt",
    version="0.1.1",
    packages=find_packages(),
    ext_modules=[module],
    cmdclass={"build_ext": BuildExt},
    install_requires=["typing_extensions>=4.0.0"],
    python_requires=">=3.11",
    author="Roman Right",
    author_email="roman-right@protonmail.com",
    description="High-Performance Data Validation for Python",
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    include_package_data=True,
)
