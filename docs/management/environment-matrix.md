# Development Environment Matrix

Verification date: 2026-09-02
Profile classification updated: 2026-09-04

Installation model: APT global; no project virtual environment.

The following facts were observed on the demonstration host using the system tools
and packages listed below. “已安装”是观测事实，不等于默认 core release 的必需项：core profile 只检查 Qt、Python/pytest 与其基础构建工具；Node/npm 属 Web optional profile，NumPy/pandas/scikit-learn/joblib 属 ML optional profile。全量 APT 安装仍可作为开发便利超集。

## Operating System

| Field | Observed value |
| --- | --- |
| Distribution | Ubuntu 25.04 |
| Version ID | 25.04 |

## Commands

| Command | Path | Observed version | Profile |
| --- | --- | --- | --- |
| git | `/usr/bin/git` | 2.48.1 | core |
| cmake | `/usr/bin/cmake` | 3.31.6 | core |
| ninja | `/usr/bin/ninja` | 1.12.1 | core |
| qmake6 | `/usr/bin/qmake6` | QMake 3.1 | core |
| qtpaths6 | `/usr/bin/qtpaths6` | 2.0 | core |
| python3 | `/usr/bin/python3` | 3.13.3 | core |
| node | `/usr/bin/node` | v20.18.1 | Web optional |
| npm | `/usr/bin/npm` | 9.2.0 | Web optional |
| pkg-config | `/usr/bin/pkg-config` | 1.8.1 | core |

## Qt pkg-config Modules

All modules resolve from `/usr/lib/x86_64-linux-gnu/pkgconfig`.

| Module | Observed version | Profile |
| --- | --- | --- |
| Qt6Core | 6.8.3 | core |
| Qt6Network | 6.8.3 | core |
| Qt6Widgets | 6.8.3 | core |
| Qt6WebEngineWidgets | 6.8.3 | core（用户端腾讯地图） |
| Qt6Charts | 6.8.3 | core（Qt 管理端） |
| Qt6Test | 6.8.3 | core |

## Global Python Modules

All imports resolve from `/usr/lib/python3/dist-packages` through `/usr/bin/python3`.

| Module | Observed version | Profile |
| --- | --- | --- |
| pytest | 8.3.5 | core（数据库/基础测试） |
| numpy | 2.2.3 | ML optional |
| pandas | 2.2.3 | ML optional |
| sklearn | 1.4.2 | ML optional |
| joblib | 1.4.2 | ML optional |

## Bootstrap APT Packages

| Package | Installed version | Profile |
| --- | --- | --- |
| git | 1:2.48.1-0ubuntu1.1 | core |
| cmake | 3.31.6-1ubuntu1 | core |
| ninja-build | 1.12.1-1 | core |
| pkg-config | 1.8.1-4 | core |
| nodejs | 20.18.1+dfsg-1ubuntu2 | Web optional |
| npm | 9.2.0~ds1-3 | Web optional |
| qt6-base-dev | 6.8.3+dfsg-0ubuntu2 | core |
| qt6-base-dev-tools | 6.8.3+dfsg-0ubuntu2 | core |
| qt6-webengine-dev | 6.8.3-0ubuntu1 | core（用户端腾讯地图） |
| qt6-charts-dev | 6.8.3-0ubuntu1 | core（Qt 管理端） |
| python3-pytest | 8.3.5-1 | core |
| python3-numpy | 1:2.2.3+ds-5 | ML optional |
| python3-pandas | 2.2.3+dfsg-8build1 | ML optional |
| python3-sklearn | 1.4.2+dfsg-8 | ML optional |
| python3-joblib | 1.4.2-3 | ML optional |
