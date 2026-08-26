# Contributing

Thanks for helping improve Wallpaper Splitter.

The build dependencies can be installed natively or supplied by the optional
devcontainer. See [DEVELOPMENT.md](DEVELOPMENT.md) for the container workflow,
local Arch test packages, and guidance for matching another environment.

## Development workflow

1. Create a focused branch from `main`.
2. Configure and build the project with tests enabled:

   ```sh
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

3. Add or update tests when changing wallpaper generation behavior.
4. Open a pull request describing the problem, the solution, and how it was
   tested.

The continuous integration workflow builds the project and runs the regression
suite for every pull request.
