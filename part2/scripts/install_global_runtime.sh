#!/usr/bin/env bash
# ⚠️ 旧基线专用（JDK 8 + Hadoop 3.2.1 + 自建 CPython 3.10.21）。
#    本组冻结基线是 JDK 17 + Hadoop 3.4.1 + Spark 3.5.7（系统 Python）。
#    在已装好冻结基线的机器上**不要执行本脚本**：
#      1) 它会写 /etc/profile.d/ev-part2.sh，与既有的 /etc/profile.d/hadoop-env.sh 造成两套环境变量叠加；
#      2) 若某个目标路径（如 /usr/local/hadoop 软链）恰巧不存在，它会把 Hadoop 指到旧版，属静默破坏。
#    新基线机器的正确做法见《06》§11.4：只建 env.sh / cluster.sh / ev-part2 三个文件即可。
#    如需保留旧环境，请自行保留 EV_PART2_* 覆盖（env.sh 已支持），不必再跑本脚本。
#
# 仅安装已校验的软件包；不下载、不覆盖既有不同工具链、不改系统 Python。
set -euo pipefail
if [[ $EUID -ne 0 || $# -ne 2 ]]; then
  echo '用法：sudo bash install_global_runtime.sh 下载目录 Hadoop归档路径' >&2
  exit 2
fi
part2_downloads="$(realpath "$1")"
part2_hadoop_archive="$(realpath "$2")"
part2_script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
verify() {
  local algo="$1" file="$2" expected="$3" actual
  actual="$("${algo}sum" "$file")"
  [[ "${actual%% *}" == "$expected" ]] || { echo "校验失败：$file" >&2; exit 1; }
}
verify sha256 "$part2_hadoop_archive" f66a3a4115b8f16c1077d1a198a06854dbef0e4233291712ed08d0a10629ed37
verify sha256 "$part2_downloads/cpython-3.10.21-20260901-x86_64.tar.gz" 73cc92db5e6fb07ba611dca3709956d57cc2ed74ebcc01515b550ea89dfe9cba
verify sha256 "$part2_downloads/OpenJDK8U-jdk_x64_linux_hotspot_8u504b01.tar.gz" 9c70e102f527ac674ac2fe9c7d47b9a04e2d19842ba5ab8e9b33f368bbadfaea
verify sha512 "$part2_downloads/spark-3.5.7-bin-hadoop3.tgz" f3b7d5974d746b9aaecb19104473da91068b698a4d292177deb75deb83ef9dc7eb77062446940561ac9ab7ee3336fb421332b1c877292dab4ac1b6ca30f4f2e0
for part2_target in /usr/local/ev-part2 /usr/local/hadoop-3.2.1 /usr/local/hadoop /usr/local/spark-3.5.7-bin-hadoop3 /usr/local/spark /usr/local/bin/ev-part2 /usr/local/bin/python3.10 /etc/profile.d/ev-part2.sh \
                   /usr/local/jdk-17 /usr/local/jdk-17.* /usr/local/hadoop-3.4.1 /usr/local/bin/python3 /etc/profile.d/hadoop-env.sh; do
  if [[ -e "$part2_target" || -L "$part2_target" ]]; then
    echo "目标已存在，拒绝覆盖；请先核对：$part2_target" >&2
    exit 1
  fi
done
# 校验归档可以完整读取后再做实际安装。
tar -tzf "$part2_hadoop_archive" >/dev/null
tar -tzf "$part2_downloads/cpython-3.10.21-20260901-x86_64.tar.gz" >/dev/null
tar -tzf "$part2_downloads/OpenJDK8U-jdk_x64_linux_hotspot_8u504b01.tar.gz" >/dev/null
tar -tzf "$part2_downloads/spark-3.5.7-bin-hadoop3.tgz" >/dev/null
install -d /usr/local/ev-part2/bin /usr/local/ev-part2/jdk8
tar -xzf "$part2_hadoop_archive" -C /usr/local --no-same-owner
tar -xzf "$part2_downloads/spark-3.5.7-bin-hadoop3.tgz" -C /usr/local --no-same-owner
tar -xzf "$part2_downloads/cpython-3.10.21-20260901-x86_64.tar.gz" -C /usr/local/ev-part2 --no-same-owner
tar -xzf "$part2_downloads/OpenJDK8U-jdk_x64_linux_hotspot_8u504b01.tar.gz" -C /usr/local/ev-part2/jdk8 --strip-components=1 --no-same-owner
ln -s /usr/local/hadoop-3.2.1 /usr/local/hadoop
ln -s /usr/local/spark-3.5.7-bin-hadoop3 /usr/local/spark
ln -s /usr/local/ev-part2/python/bin/python3.10 /usr/local/bin/python3.10
install -m 644 "$part2_script_dir/ev-part2-env.sh" /usr/local/ev-part2/env.sh
install -m 644 "$part2_script_dir/ev-part2-env.sh" /etc/profile.d/ev-part2.sh
install -m 755 "$part2_script_dir/ev-part2" /usr/local/bin/ev-part2
install -m 755 "$part2_script_dir/cluster.sh" "$part2_script_dir/configure_local.py" /usr/local/ev-part2/bin/
install -m 644 "$part2_script_dir/pyspark-global.pth" /usr/local/ev-part2/python/lib/python3.10/site-packages/ev-part2-pyspark.pth
/usr/local/ev-part2/python/bin/python3.10 --version
/usr/local/ev-part2/jdk8/bin/java -version
echo '全局安装完成；普通用户执行 ev-part2 configure / ev-part2 start。不要以 root 启动业务服务。'
