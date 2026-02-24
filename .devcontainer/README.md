# DevContainers

- [DevContainers](#devcontainers)
  - [Overview](#overview)
  - [Configurations](#configurations)
      - [Image Details](#image-details)
      - [Getting Started](#getting-started)

## Overview

Two devcontainer configurations are provided, one for each supported Bazel major version:

| Configuration | Bazel Version | Prompt Name |
|---|---|---|
| `bazel6/` | 6.4.0 | `bazel6-zen` |
| `bazel7/` | 7.5.0 | `bazel7-zen` |

Both share a single `Dockerfile` at `.devcontainer/Dockerfile`. The Bazel version is passed via the `BAZEL_VERSION` build arg in each `devcontainer.json`.

To use a different patch version, edit the `BAZEL_VERSION` value in the corresponding `devcontainer.json`.

## Configurations

:warning: This is as yet an unsupported workflow! YMMV.

#### Image Details

* Base Image: `focal` (ubuntu)
* Bazel:
  * Installed via Bazelisk.
  * `bazel` and `bazelisk` invocations both work (a bash alias supports this).
  * The Bazel version is configured via the `BAZEL_VERSION` build arg in each configuration's `devcontainer.json`.

#### Getting Started

Locally:

* [Install VSCode](https://code.visualstudio.com/docs/setup/linux#_debian-and-ubuntu-based-distributions)
* Open the project in VSCode
* CTRL-SHIFT-P &rarr; Reopen in Container
* Select either **Bazel 6** or **Bazel 7** from the configuration picker
* Open a terminal in the container and run

```
(docker) zen@bazel6-zen:/workspaces/maliput_geopackage$ bazel build //...
```

CodeSpaces:

* Go to Codespaces
* Select `New with Options`
* Select **Bazel 6** or **Bazel 7** from the `Dev Container Configuration`

<img src="./resources/codespaces.png" alt="codespaces" width="600">

* Open a terminal in the container and run

```
@<github-username> ➜ /workspaces/maliput_geopackage (main) $ bazel build //...
```
