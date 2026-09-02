# Development Environment Matrix

Verification date: 2026-09-02

Installation model: APT global; no project virtual environment.

The following facts were observed on the demonstration host using the system tools
and packages listed below.

## Operating System

| Field | Observed value |
| --- | --- |
| Distribution | Ubuntu 25.04 |
| Version ID | 25.04 |

## Commands

| Command | Path | Observed version |
| --- | --- | --- |
| git | `/usr/bin/git` | 2.48.1 |
| cmake | `/usr/bin/cmake` | 3.31.6 |
| ninja | `/usr/bin/ninja` | 1.12.1 |
| qmake6 | `/usr/bin/qmake6` | QMake 3.1 |
| qtpaths6 | `/usr/bin/qtpaths6` | 2.0 |
| python3 | `/usr/bin/python3` | 3.13.3 |
| node | `/usr/bin/node` | v20.18.1 |
| npm | `/usr/bin/npm` | 9.2.0 |
| pkg-config | `/usr/bin/pkg-config` | 1.8.1 |

## Qt pkg-config Modules

All modules resolve from `/usr/lib/x86_64-linux-gnu/pkgconfig`.

| Module | Observed version |
| --- | --- |
| Qt6Core | 6.8.3 |
| Qt6Network | 6.8.3 |
| Qt6Widgets | 6.8.3 |
| Qt6WebEngineWidgets | 6.8.3 |
| Qt6Charts | 6.8.3 |
| Qt6Test | 6.8.3 |

## Global Python Modules

All imports resolve from `/usr/lib/python3/dist-packages` through `/usr/bin/python3`.

| Module | Observed version |
| --- | --- |
| pytest | 8.3.5 |
| numpy | 2.2.3 |
| pandas | 2.2.3 |
| sklearn | 1.4.2 |
| joblib | 1.4.2 |

## Bootstrap APT Packages

| Package | Installed version |
| --- | --- |
| git | 1:2.48.1-0ubuntu1.1 |
| cmake | 3.31.6-1ubuntu1 |
| ninja-build | 1.12.1-1 |
| pkg-config | 1.8.1-4 |
| nodejs | 20.18.1+dfsg-1ubuntu2 |
| npm | 9.2.0~ds1-3 |
| qt6-base-dev | 6.8.3+dfsg-0ubuntu2 |
| qt6-base-dev-tools | 6.8.3+dfsg-0ubuntu2 |
| qt6-webengine-dev | 6.8.3-0ubuntu1 |
| qt6-charts-dev | 6.8.3-0ubuntu1 |
| python3-pytest | 8.3.5-1 |
| python3-numpy | 1:2.2.3+ds-5 |
| python3-pandas | 2.2.3+dfsg-8build1 |
| python3-sklearn | 1.4.2+dfsg-8 |
| python3-joblib | 1.4.2-3 |
