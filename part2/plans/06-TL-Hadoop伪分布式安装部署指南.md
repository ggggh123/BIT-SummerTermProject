# 06 · Hadoop 伪分布式安装部署指南（#2 TL）

> 承担人：#2（TL，技术负责人）
> 用途：第二阶段全组 5 台成员机的**统一环境搭建手册**，也是《02》§2「环境基线」的落地文档
> **本指南不依赖老师下发的安装包与指导书**：全部组件使用官方发行版 tarball 安装，版本自主可控

---

## 1. 适用范围与部署形态

### 1.1 部署形态（全组统一）

- **5 台成员机各自安装单机伪分布式 Hadoop，互不组网**，用于各自模块的本地开发与自测；
- 全链路（生成器 → HDFS → 质量/清洗 → 数仓分层 → Flask → 前端大屏）固定在**一台集成/演示机**上端到端跑通；
- **如实声明**：本方案为**单机伪分布式**（单机多进程，具备完整 NameNode / DataNode / YARN 体系），**不是多节点集群**。

### 1.2 为什么不用老师的安装包

老师下发的 `hadoop-3.2.1` + `jdk-8u261` 版本偏旧，且旧 JDK 与新版 Spark 存在「Java 8 < 8u371 已弃用」等问题；本组改用**官方 tarball 自行安装**，锁定 Hadoop 3.4.1 + JDK 17 + Spark 3.5.7 的组合。

> 老师的《01 安装Hadoop（Ubuntu 伪分布式）_指导书.pdf》仅作流程参考（其正文里 `hadoop-3.3.1` / `jdk-8u181` 等描述与实际包也不一致），命令与配置以本指南为准。

## 2. 版本基线（全组必须一致）

| 项 | 基线值 | 说明 |
|---|---|---|
| 操作系统 | **Ubuntu 22.04 LTS** 或 **Ubuntu 25.04**（双轨支持） | 见 §3 |
| JDK | **Eclipse Temurin JDK 17**（tarball） | 统一重命名为 `/usr/local/jdk-17` |
| Hadoop | **3.4.1** | 统一重命名为 `/usr/local/hadoop` |
| Spark | **3.5.7**（`spark-3.5.7-bin-hadoop3.tgz`） | 统一重命名为 `/usr/local/spark` |
| Python | 22.04：系统 **3.10**；25.04：**自建 3.11**（见 §3.3） | PySpark 用 |
| 主机名规则 | 姓名全拼 + 两位数字，如 `zhangsan01` | 写入 `core-site.xml` 等处 |
| 操作用户 | `hadoop`（密码 `hadoop`，已授予 sudo） | — |
| HDFS RPC（`fs.defaultFS`） | `hdfs://<主机名>:8020` | 与指导书一致 |
| NameNode Web UI | `http://localhost:9870/` | — |
| ResourceManager Web UI | `http://localhost:8088/` | — |
| ResourceManager RPC | `<主机名>:8032` | — |
| SecondaryNameNode HTTP | `<主机名>:9868` | — |
| 副本数 | `dfs.replication = 1` | 单机伪分布式 |
| HDFS 业务根目录 | `/ev-charging/` | 见《02》§2.3 |

### 2.1 选型依据

| 决策 | 理由 |
|---|---|
| **Hadoop 3.4.1** | **3.4.0 起官方支持 Java 17** runtime（3.3.x 只到 Java 11），是当前 3.x 稳定线 |
| **JDK 17（Temurin）** | Hadoop 3.4.1 与 Spark 3.5.7 **共同支持**的 LTS 版本；比 JDK 8/11 新，且是长期支持版 |
| **Spark 3.5.7** | 官方支持 Java 8/11/17；3.5 是 3.x 最后一支，稳定、文档全，与课程资料完全兼容 |
| **不用 Spark 4.x** | 4.x 同样要求 Java 17，但 API/依赖变动较大；3 天工期下选更稳的 3.5.x |
| **不用老师 tarball** | 版本旧，且与新版 Spark 存在兼容与弃用问题 |

> 若日后要上 Spark 4.x：它同样要求 Java 17，可平滑替换，替换后重跑一次 §7 自检即可。

## 3. 前置准备（Ubuntu 双轨）

### 3.1 两版通用

- 内存 ≥ 8 GB（推荐 12 GB），CPU ≥ 4 核，磁盘 ≥ 60 GB（Hadoop + Spark + 数据建议留 30 GB 以上）；
- 需要**能访问外网**下载安装包；若目标机无外网，见 §10 离线准备；
- 网络：NAT 或桥接均可（伪分布式用不到跨机通信）。

### 3.2 Ubuntu 22.04 LTS（推荐）

软件源正常，直接安装依赖即可：

```bash
sudo apt update
sudo apt install -y openssh-server openssh-client curl
```

Python 用系统自带 **3.10**，无需额外处理。

### 3.3 Ubuntu 25.04（已 EOL，需三步额外处理）

> Ubuntu 25.04 已于 **2026-01-15 结束支持**，普通软件源会 404，必须切到 `old-releases`；且它默认 Python 3.13，**不用于 PySpark**。

**第 1 步：软件源切到 old-releases**

```bash
sudo sed -i 's|http://archive.ubuntu.com|http://old-releases.ubuntu.com|g'   /etc/apt/sources.list.d/ubuntu.sources
sudo sed -i 's|https://archive.ubuntu.com|http://old-releases.ubuntu.com|g'  /etc/apt/sources.list.d/ubuntu.sources
sudo sed -i 's|http://security.ubuntu.com|http://old-releases.ubuntu.com|g'  /etc/apt/sources.list.d/ubuntu.sources
sudo sed -i 's|https://security.ubuntu.com|http://old-releases.ubuntu.com|g' /etc/apt/sources.list.d/ubuntu.sources
sudo apt update
sudo apt install -y openssh-server openssh-client curl
```

**第 2 步：自建 Python 3.11（不依赖 apt）**

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
source $HOME/.local/bin/env
uv python install 3.11
```

**第 3 步：创建 3.11 虚拟环境（后续 PySpark 一律用它）**

```bash
uv venv --python 3.11 ~/venv-py311
source ~/venv-py311/bin/activate
uv pip install pyspark==3.5.7 pandas pyarrow
```

> 记住这个路径 `~/venv-py311/bin/python`，§6 里要把它写进 `PYSPARK_PYTHON`。

## 4. 获取安装包（全部走 tarball）

| 组件 | 文件名 | 下载地址 |
|---|---|---|
| JDK 17 | `OpenJDK17U-jdk_x64_linux_hotspot_17.0.x_y.tar.gz` | Adoptium（Eclipse Temurin）发布页选 **Linux / x64 / .tar.gz**：`https://adoptium.net/temurin/releases/?version=17` |
| Hadoop 3.4.1 | `hadoop-3.4.1.tar.gz` | `https://archive.apache.org/dist/hadoop/common/hadoop-3.4.1/hadoop-3.4.1.tar.gz` |
| Spark 3.5.7 | `spark-3.5.7-bin-hadoop3.tgz` | `https://archive.apache.org/dist/spark/spark-3.5.7/spark-3.5.7-bin-hadoop3.tgz` |

> **国内下载慢**：可换 Apache 镜像站（如清华 TUNA `https://mirrors.tuna.tsinghua.edu.cn/apache/`），文件名一致。

下载后记录校验和，全组比对确认是同一份包：

```bash
sha256sum OpenJDK17U-*.tar.gz hadoop-3.4.1.tar.gz spark-3.5.7-bin-hadoop3.tgz | tee ~/install_pkgs.sha256
```

上传到虚拟机的任意目录（推荐 `/home/hadoop/`）。上传方式三选一：VMware 共享文件夹 / `scp`（配合 FinalShell、Xftp 等）/ 拖拽到远程桌面。

## 5. Hadoop 安装步骤（命令级）

> 以下命令中 `<主机名>` 请替换为自己的主机名（如 `zhangsan01`）。

### 5.1 创建 hadoop 用户并授权

```bash
sudo su root                     # 初始密码按实际
cat /etc/passwd | grep hadoop    # 已存在则跳过创建
useradd -m hadoop -s /bin/bash
adduser hadoop sudo
```

设置密码 —— **注意 Ubuntu 默认开启 PAM 密码强度校验**，直接 `passwd hadoop` 或 `echo "hadoop:hadoop" | chpasswd` 设 6 个字符会被拒绝并报「无效的密码：密码少于 8 个字符」（`chpasswd` 还会**静默失败**，用户实际处于无密码状态）。用下面这条绕过：

```bash
# 主方案：直接写入 SHA-512 哈希，绕过 PAM 强度检查
sudo usermod -p "$(openssl passwd -6 hadoop)" hadoop
sudo passwd -S hadoop            # 输出第二列含 "P" 表示密码已生效

# 备选：交互式设置（密码须 ≥ 8 字符，否则同样被 PAM 拒绝）
# sudo passwd hadoop
```

> 该密码仅在本地虚拟机使用。日常操作统一走 `sudo -u hadoop`（以已有 sudo 用户切换身份，**不需要 hadoop 的密码**），只有 `su - hadoop` 或图形界面登录 hadoop 账户时才会用到。

### 5.2 修改主机名

```bash
# 主方案：立即生效，无需重启（通过 SSH 远程部署时 reboot 会中断会话）
sudo hostnamectl set-hostname zhangsan01    # 替换为自己的「姓名全拼+数字」
hostname                                    # 确认输出新主机名

# 备选：改配置文件后重启（等效，但会断开远程连接）
# sudo gedit /etc/hostname && reboot
```

> 主机名会写入 `core-site.xml` / `yarn-site.xml`，**必须在装 Hadoop 前定好**；中途改名需同步修改配置文件并重跑 §7 自检。

### 5.3 添加域名映射

```bash
sudo su root
ip a                             # 记下本机 IP
sudo gedit /etc/hosts
```

编辑为（第一行保留 `127.0.0.1 localhost`，多余行删除）：

```text
127.0.0.1   localhost
<本机IP>    <主机名>            # 例：172.22.252.3  zhangsan01
```

### 5.4 SSH 免密登录

```bash
su hadoop
ssh-keygen -t rsa                # 一路回车
cd ~/.ssh
ssh-copy-id -i id_rsa.pub hadoop@<主机名>
ssh hadoop@<主机名>              # 应免密进入
exit
```

> 伪分布式也需要免密：`start-dfs.sh` / `stop-dfs.sh` 会通过 SSH 连接本机启停守护进程。

### 5.5 安装 JDK 17

```bash
su hadoop
sudo tar -zxvf OpenJDK17U-jdk_x64_linux_hotspot_17.0.x_y.tar.gz -C /usr/local
# 解压后目录名形如 jdk-17.0.x+y，统一改名去掉 "+" 等符号，避免路径问题
sudo mv /usr/local/jdk-17.0.* /usr/local/jdk-17
sudo chown -R hadoop:hadoop /usr/local/jdk-17
```

**配置环境变量（关键：写全局 `profile.d`，不要只写 `~/.bashrc`）**：

```bash
# 1) 全局生效：登录 shell 会加载 /etc/profile.d/*.sh
sudo tee /etc/profile.d/hadoop-env.sh > /dev/null <<'EOF'
export JAVA_HOME=/usr/local/jdk-17
export PATH=$JAVA_HOME/bin:$PATH
EOF
sudo chmod 644 /etc/profile.d/hadoop-env.sh

# 2) 让 hadoop 用户的交互式 shell 也加载它
sudo -u hadoop bash -c 'grep -q hadoop-env ~/.bashrc || echo "[ -f /etc/profile.d/hadoop-env.sh ] && . /etc/profile.d/hadoop-env.sh" >> ~/.bashrc'
```

```bash
java -version                    # 期望：openjdk version "17.0.x"
```

> **为什么不能只写 `~/.bashrc`**：`ssh <主机> '<命令>'` 执行的是**非交互、非登录** shell，不会读取 `~/.bashrc`，会出现「本地能跑、SSH 过去报 command not found」。本组的作业脚本、`start_part2.sh` 都是远程/非交互调用的，因此 JDK / Hadoop / Spark 的环境变量（§5.5、§5.6、§6）**统一追加到同一个 `/etc/profile.d/hadoop-env.sh`**。
>
> 脚本内需要环境变量时，显式加一行 `. /etc/profile.d/hadoop-env.sh`，或改用 `bash -lc` 执行。

### 5.6 安装 Hadoop 3.4.1

```bash
sudo tar -zxvf hadoop-3.4.1.tar.gz -C /usr/local
sudo mv /usr/local/hadoop-3.4.1 /usr/local/hadoop
sudo chown -R hadoop:hadoop /usr/local/hadoop
```

把 Hadoop 变量**追加到同一个全局文件** `/etc/profile.d/hadoop-env.sh`（与 §5.5 同一个文件）：

```bash
sudo tee -a /etc/profile.d/hadoop-env.sh > /dev/null <<'EOF'
export HADOOP_HOME=/usr/local/hadoop
export HADOOP_CONF_DIR=$HADOOP_HOME/etc/hadoop
export YARN_HOME=$HADOOP_HOME
export YARN_CONF_DIR=$HADOOP_CONF_DIR
export PATH=$HADOOP_HOME/bin:$HADOOP_HOME/sbin:$PATH
EOF
```

```bash
hadoop version                   # 期望：Hadoop 3.4.1
```

### 5.7 配置 7 个文件

```bash
cd /usr/local/hadoop/etc/hadoop
```

**（1）`hadoop-env.sh`** —— Java 路径 + **Java 17 必需的 `--add-opens`**：

```bash
export JAVA_HOME=/usr/local/jdk-17

# Java 17 起 JDK 强封装，Hadoop 反射访问会被拒绝（InaccessibleObjectException），必须放开以下包
export JDK_ADD_OPENS="--add-opens java.base/java.lang=ALL-UNNAMED --add-opens java.base/java.lang.reflect=ALL-UNNAMED --add-opens java.base/java.io=ALL-UNNAMED --add-opens java.base/java.net=ALL-UNNAMED --add-opens java.base/java.nio=ALL-UNNAMED --add-opens java.base/java.util=ALL-UNNAMED --add-opens java.base/java.util.concurrent=ALL-UNNAMED --add-opens java.base/java.util.concurrent.atomic=ALL-UNNAMED --add-opens java.base/sun.nio.ch=ALL-UNNAMED --add-opens java.base/sun.security.action=ALL-UNNAMED --add-opens java.base/jdk.internal.ref=ALL-UNNAMED --add-opens java.security.jgss/sun.security.krb5=ALL-UNNAMED"

export HDFS_NAMENODE_OPTS="$JDK_ADD_OPENS"
export HDFS_DATANODE_OPTS="$JDK_ADD_OPENS"
export HDFS_SECONDARYNAMENODE_OPTS="$JDK_ADD_OPENS"
export HADOOP_OPTS="$JDK_ADD_OPENS"
```

**（2）`yarn-env.sh`**：

```bash
export JAVA_HOME=/usr/local/jdk-17
export JDK_ADD_OPENS="--add-opens java.base/java.lang=ALL-UNNAMED --add-opens java.base/java.util=ALL-UNNAMED --add-opens java.base/java.util.concurrent=ALL-UNNAMED --add-opens java.base/java.net=ALL-UNNAMED --add-opens java.base/java.nio=ALL-UNNAMED"
export YARN_RESOURCEMANAGER_OPTS="$JDK_ADD_OPENS"
export YARN_NODEMANAGER_OPTS="$JDK_ADD_OPENS"
```

**（3）`workers`** —— 单机伪分布式，写入本机主机名（一行）：

```text
<主机名>
```

**（4）`core-site.xml`**：

```xml
<configuration>
  <property>
    <name>fs.defaultFS</name>
    <value>hdfs://<主机名>:8020</value>
  </property>
  <property>
    <name>hadoop.tmp.dir</name>
    <value>file:/usr/local/hadoop/tmp</value>
  </property>
</configuration>
```

**（5）`hdfs-site.xml`**：

```xml
<configuration>
  <property>
    <name>dfs.namenode.secondary.http-address</name>
    <value><主机名>:9868</value>
  </property>
  <property>
    <name>dfs.namenode.name.dir</name>
    <value>file:///usr/local/hadoop/dfs/name</value>
  </property>
  <property>
    <name>dfs.datanode.data.dir</name>
    <value>file:///usr/local/hadoop/dfs/data</value>
  </property>
  <property>
    <name>dfs.replication</name>
    <value>1</value>
  </property>
</configuration>
```

**（6）`mapred-site.xml`** —— 含 Java 17 下 MapReduce 容器所需的 `--add-opens`：

```xml
<configuration>
  <property>
    <name>mapreduce.framework.name</name>
    <value>yarn</value>
  </property>
  <property>
    <name>yarn.app.mapreduce.am.command-opts</name>
    <value>-Xmx1024m --add-opens java.base/java.lang=ALL-UNNAMED</value>
  </property>
  <property>
    <name>mapreduce.map.java.opts</name>
    <value>-Xmx1024m --add-opens java.base/java.lang=ALL-UNNAMED</value>
  </property>
  <property>
    <name>mapreduce.reduce.java.opts</name>
    <value>-Xmx1024m --add-opens java.base/java.lang=ALL-UNNAMED</value>
  </property>
</configuration>
```

**（7）`yarn-site.xml`** —— 含内存上限，**内存 ≤ 8 GB 的机器必须设**，否则 YARN 容器申请不到资源、作业反复失败：

```xml
<configuration>
  <property>
    <name>yarn.nodemanager.aux-services</name>
    <value>mapreduce_shuffle</value>
  </property>
  <property>
    <name>yarn.nodemanager.aux-services.mapreduce.shuffle.class</name>
    <value>org.apache.hadoop.mapred.ShuffleHandler</value>
  </property>
  <property>
    <name>yarn.resourcemanager.address</name>
    <value><主机名>:8032</value>
  </property>
  <property>
    <name>yarn.nodemanager.resource.memory-mb</name>
    <value>4096</value>
  </property>
  <property>
    <name>yarn.scheduler.maximum-allocation-mb</name>
    <value>4096</value>
  </property>
  <property>
    <name>yarn.nodemanager.env-whitelist</name>
    <value>JAVA_HOME,HADOOP_COMMON_HOME,HADOOP_HDFS_HOME,HADOOP_CONF_DIR,CLASSPATH_PREPEND_DISTCACHE,HADOOP_YARN_HOME,HADOOP_HOME,PATH,LANG,TZ,HADOOP_MAPRED_HOME</value>
  </property>
</configuration>
```

> 两个内存属性的取值依据见 §10.2；虚拟机内存紧张时可下调至 2048。

### 5.8 格式化 NameNode

```bash
cd /usr/local/hadoop
bin/hdfs namenode -format
```

### 5.9 启动服务并验证进程

```bash
sbin/start-dfs.sh
sbin/start-yarn.sh
jps
```

`jps` 应显示 **5 个进程**：

```text
NameNode
SecondaryNameNode
DataNode
ResourceManager
NodeManager
```

### 5.10 验证 Web UI 与 HDFS 读写

```bash
# 浏览器访问
#   NameNode：       http://localhost:9870/
#   ResourceManager：http://localhost:8088/

bin/hdfs dfs -mkdir -p /ev-charging
bin/hdfs dfs -mkdir /input
bin/hdfs dfs -put etc/hadoop/*.xml /input
bin/hdfs dfs -ls /input
```

### 5.11 MapReduce 冒烟测试

```bash
bin/hadoop jar share/hadoop/mapreduce/hadoop-mapreduce-examples-3.4.1.jar grep /input /output 'dfs[a-z.]+'
bin/hdfs dfs -cat /output/*
bin/hadoop jar share/hadoop/mapreduce/hadoop-mapreduce-examples-3.4.1.jar pi 2 1000
```

### 5.12 停止服务

```bash
sbin/stop-yarn.sh
sbin/stop-dfs.sh
```

## 6. Spark 安装与打通

> **版本基线：Spark 3.5.7（`spark-3.5.7-bin-hadoop3.tgz`）**，全组必须完全一致（含小版本号）。

```bash
sudo tar -zxvf spark-3.5.7-bin-hadoop3.tgz -C /usr/local
sudo mv /usr/local/spark-3.5.7-bin-hadoop3 /usr/local/spark
sudo chown -R hadoop:hadoop /usr/local/spark
```

把 Spark 变量**追加到全局文件** `/etc/profile.d/hadoop-env.sh`（与 §5.5、§5.6 同一个文件）：

```bash
sudo tee -a /etc/profile.d/hadoop-env.sh > /dev/null <<'EOF'
export SPARK_HOME=/usr/local/spark
export PATH=$SPARK_HOME/bin:$PATH
EOF
```

配置 `spark-env.sh`：

```bash
cd /usr/local/spark/conf
cp spark-env.sh.template spark-env.sh
```

写入：

```bash
export JAVA_HOME=/usr/local/jdk-17
export HADOOP_CONF_DIR=/usr/local/hadoop/etc/hadoop
# 让 Spark 直接用本机安装的 Hadoop 3.4.1 客户端 jar，避免与 Spark 自带版本错配
export SPARK_DIST_CLASSPATH=$(/usr/local/hadoop/bin/hadoop classpath)

# Python 解释器：22.04 用 python3；25.04 指向自建的 3.11 虚拟环境
export PYSPARK_PYTHON=python3
# 25.04 改为：export PYSPARK_PYTHON=/home/hadoop/venv-py311/bin/python
export PYSPARK_DRIVER_PYTHON=$PYSPARK_PYTHON
```

验证（HDFS 已启动的前提下）：

```bash
pyspark --master yarn
>>> spark.read.text("hdfs://<主机名>:8020/input/*.xml").count()
```

> Spark 3.5 在 Java 17 下会**自动附加**所需的 `--add-opens`，无需手工配置。

## 7. 环境自检脚本 `check_env.sh`

任一台成员机部署完成后运行本脚本，全部 `[OK]` 即为合格（这也是《02》验收「成员机级」的判定依据）。本组实测记录见 §11。

**运行方式（必须以 hadoop 身份、用 login shell 运行，否则拿不到全局环境变量）**：

```bash
sudo -u hadoop bash -lc "bash ~/check_env.sh"
```

**两个容易误报 FAIL 的点**：

- **`add-opens 已配` 项**：脚本用 `grep -c 'add-opens' hadoop-env.sh` 判定，期望结果为 `1`。若 §5.7(1) 追加段的**注释里也出现 `add-opens` 字样**，计数会变成 2 而误报 FAIL——注释请写「Java 17 JVM 参数」之类措辞。
- **`hosts 含本机名` 项**：要求 `/etc/hosts` 中本机名恰好出现 **1** 次，需删除残留的 `127.0.1.1 <旧主机名>` 行。

```bash
#!/usr/bin/env bash
# check_env.sh —— 第二阶段 Hadoop 伪分布式环境自检
set -u
HOST=$(hostname)
pass=0; fail=0
chk() { # $1=描述 $2=实际 $3=期望子串
  if echo "$2" | grep -q "$3"; then echo "[OK]   $1"; pass=$((pass+1));
  else echo "[FAIL] $1  (实际: $2)"; fail=$((fail+1)); fi
}

echo "=== 主机名: $HOST ==="
chk "JDK 17"          "$(java -version 2>&1 | head -1)"          "17"
chk "Hadoop 3.4.1"    "$(hadoop version | head -1)"               "3.4.1"
chk "Spark 3.5.7"     "$(spark-submit --version 2>&1 | grep -m1 version)" "3.5.7"
chk "Python 3"        "$(python3 --version)"                      "3."
chk "hosts 含本机名"   "$(grep -c "$HOST" /etc/hosts)"             "1"
chk "core-site 端口"   "$(grep -A1 fs.defaultFS ${HADOOP_CONF_DIR}/core-site.xml | tail -1)" ":8020"
chk "副本数=1"         "$(grep -A1 dfs.replication ${HADOOP_CONF_DIR}/hdfs-site.xml | tail -1)" "1"
chk "add-opens 已配"   "$(grep -c 'add-opens' ${HADOOP_CONF_DIR}/hadoop-env.sh)" "1"
chk "SSH 免密"         "$(ssh -o BatchMode=yes -o StrictHostKeyChecking=no hadoop@$HOST echo ok 2>/dev/null)" "ok"

echo "=== jps 进程检查 ==="
JP=$(jps)
for p in NameNode DataNode SecondaryNameNode ResourceManager NodeManager; do
  chk "进程 $p" "$JP" "$p"
done
echo "=== HDFS 读写检查 ==="
chk "HDFS 可写" "$(hdfs dfs -mkdir -p /ev-charging/_health && hdfs dfs -touchz /ev-charging/_health/ok && echo ok)" "ok"

echo "-----------------------------------------"
echo "通过 $pass 项，失败 $fail 项"
[ "$fail" -eq 0 ] && echo "环境自检 PASS" || echo "环境自检 FAIL，请按第 8 节排查"
```

## 8. 常见问题排查

| 现象 | 原因 | 处理 |
|---|---|---|
| 25.04 上 `apt update` 报 404 | 该版本已 EOL，普通源失效 | 按 §3.3 第 1 步把源切到 `old-releases.ubuntu.com` |
| YARN/NameNode 启动报 `InaccessibleObjectException` | Java 17 强封装，缺 `--add-opens` | 按 §5.7（1）（2）补齐配置后重启 |
| `jps` 缺 NameNode | 未格式化或格式化失败 | 检查 `/usr/local/hadoop/dfs/name`；`bin/hdfs namenode -format` |
| `jps` 缺 DataNode | clusterID 不一致（重复格式化导致） | `rm -rf /usr/local/hadoop/dfs/data/*` 后重启 `start-dfs.sh` |
| 写入 HDFS 报安全模式 | 磁盘/副本未达阈值 | `hdfs dfsadmin -safemode get` 查看；`hdfs dfsadmin -safemode leave` 关闭 |
| `start-dfs.sh` 报 SSH 连不上 | 免密未配置或主机名不在 `/etc/hosts` | 重做 §5.3、§5.4 |
| Web UI 打不开 | 进程未起或端口被占 | `jps` + `netstat -tunlp \| grep 9870` |
| 连不上 HDFS（写了 9000 端口） | 端口写错 | 本方案统一为 **8020**，改 `core-site.xml` 后重启 |
| Spark 读不到 HDFS | 未配置 `HADOOP_CONF_DIR` / `SPARK_DIST_CLASSPATH` | 见 §6 的 `spark-env.sh` |
| PySpark 报 Python 版本/模块错误 | 用了 25.04 的系统 Python 3.13 | 把 `PYSPARK_PYTHON` 指向自建的 3.11 虚拟环境 |
| 安装包中文/权限报错 | 上传时码流或属主不对 | `chown -R hadoop:hadoop` 后重试 |

### 8.1 PySpark 读 CSV 的四个真实坑（老师参考实现已验证）

| 现象 | 原因 | 处理 |
|---|---|---|
| 列名匹配失败 / 整列取不到值 | CSV **表头带单位**（如 `pack_voltage (V)`、`max_temperature (℃)`） | schema 先按**原始表头名**建列，读入后 `.toDF(*规范名)` 改名 |
| 一整列全是 `null` | 列里有小数（如 `soc = 15.2`），schema 却定成 `IntegerType` | 数值列按实际取值定 `FloatType`/`DoubleType`，宁可宽不可窄 |
| `toPandas()` 报 `OSError: [Errno 22]` | 时间列被推断为 `Timestamp`，但年份异常（如 `0014-11-18`），超出 datetime 范围 | **时间列一律按 `StringType` 读入**（呼应《03》§2.2 的 ODS 全字段 StringType），清洗阶段再显式解析 |
| 首列列名带 `\ufeff` | 上游用 pandas 写出，`utf-8-sig` 带 BOM | 读入后统一去 BOM（`withColumnRenamed`） |

> 这几类正是《03》质量注入 Q1/Q3/Q4/Q5 所模拟的真实场景，可用于答辩说明「清洗规则来自真实数据经验」。

## 9. 与第二阶段项目的对接

1. **HDFS 业务目录**：按《02》§2.3 规划创建。

   ```bash
   hdfs dfs -mkdir -p /ev-charging/{ods,dwd,dws,ads,quality,forecast}
   ```

2. **Spark 读写**：统一用 `hdfs://<主机名>:8020/ev-charging/...`，并在作业里显式指定，避免依赖默认值。

3. **数据交接**：HDFS 数据与 Parquet 产物**不进 git**，导出到共享文件夹的 `handoff/` 各子目录（`ods / dwd / dws / ads / forecast`），每包附 `manifest.json`（行数 + `sha256` + `seed`），接收方校验通过后方可开工。详见《02》§2.4。

4. **集成/演示机**：全组指定一台作为唯一权威全链路机器，答辩在该机演示；拉伸目标是把本指南复制到每台成员机，实现「每台都能独立跑全链路」。
   - 集成机**不是特殊机型**：Hadoop/Spark 环境基线与成员机完全一致，升级只需**加装应用层依赖**（Node 18+ 与前端构建产物、`flask`/`flask-cors`/`pandas`/`pyarrow`）并**收齐 `handoff/` 各包**（含校验）。完整差异对照与升级检查清单见《02》§2.4。

5. **老师参考实现不能照抄**：老师下发的 `demo_ods.py / demo_dwd.py / demo_dws.py / demo_ads.py / dw_common.py` 用的是 **`local[*]` 本地模式 + 本地 CSV 读写**，只用于理解四层划分与业务口径。本项目要求作业**跑在 Hadoop（HDFS + YARN）**上，照抄必须改三处：
   - `SparkSession ... .master("local[*]")` → 提交时改用 `--master yarn`（`spark-submit --master yarn ...`）；
   - `save_csv()`（`toPandas()` 中转写本地文件）→ 改为 `df.write.parquet("hdfs://<主机名>:8020/ev-charging/dwd/...")`；
   - 读写路径由本地相对路径 → `hdfs://<主机名>:8020/...` 绝对路径。

   另：参考实现的 ADS 落 MySQL，本项目**统一落 SQLite 单文件**（见《04》§3.3），无需 MySQL 服务与 JDBC 驱动。

## 10. 离线依赖准备与资源基线（答辩前必做）

### 10.1 为什么必须离线准备

演示环境可能**无外网**。下列依赖须提前下载并在组内共享，避免现场装不上：

| 依赖 | 用途 | 获取建议 |
|---|---|---|
| `hadoop-3.4.1.tar.gz` | Hadoop 环境 | Apache Archive（或 TUNA 镜像） |
| `OpenJDK17U-jdk_x64_linux_hotspot_17.0.x_y.tar.gz` | JDK | Adoptium 发布页（Linux x64 tar.gz） |
| `spark-3.5.7-bin-hadoop3.tgz` | PySpark / SparkSQL / MLlib | Apache Archive |
| Python wheels：`pyspark==3.5.7`、`flask`、`flask-cors`、`pandas`、`pyarrow`、`faker` | 后端与数据处理 | 联网机上执行 `pip download -d wheels/ -r requirements.txt`，离线 `pip install --no-index --find-links=wheels/` |
| 前端 `node_modules`（vue、vite、axios、echarts） | 前端构建 | 联网机 `npm install` 后**整体打包** `node_modules`；或 `npm ci --offline` |
| 北京市地图 GeoJSON + ECharts 静态包 | 地图散点图底图 | 提前下载为本地文件，**不走 CDN** |

> **不需要安装的**：MySQL 服务与 MySQL JDBC 驱动**本项目一律不用**——ADS 结果落 **SQLite 单文件**，Python 标准库 `sqlite3` 自带，零依赖。这样即使答辩机无外网也能一键起来。

### 10.2 资源基线（防 OOM）

| 项 | 建议值 |
|---|---|
| VM 内存 | ≥ 8 GB（推荐 12 GB） |
| VM 磁盘 | ≥ 60 GB |
| Spark 参数 | `--executor-memory 2g --driver-memory 2g`；`spark.sql.shuffle.partitions=8`（单机伪分布式不要用默认 200） |
| YARN 容器 | `yarn.nodemanager.resource.memory-mb=4096`、`yarn.scheduler.maximum-allocation-mb=4096`（内存紧张时下调） |

> 若内存不足导致 YARN 作业反复失败：可临时用 `pyspark --master local[*]` 直连 HDFS 跑通逻辑，**答辩前切回 `--master yarn` 并留 YARN 记录**（见《00》§10 风险）。

---

## 11. 实机部署记录（本组）

> 记录人：#2（TL）；部署机：`niyujun01`（**集成/演示机**）；部署日期：2026-09-14

### 11.1 实际环境

| 项 | 实测值 |
|---|---|
| 虚拟机 | VMware 17 / Ubuntu **22.04.3 LTS** / 8 核 / 内存 7.7 GiB / 磁盘 491 GiB |
| 主机名 / IP | `niyujun01` / `192.168.59.128`（VMware NAT） |
| 操作账户 | `bit`（原账户，已配置免密 sudo）+ `hadoop`（密码 `hadoop`，已加入 sudo 组） |
| JDK | Eclipse Temurin **17.0.20.1+1** → `/usr/local/jdk-17` |
| Hadoop | **3.4.1** → `/usr/local/hadoop`（1.7 GiB） |
| Spark | **3.5.7** → `/usr/local/spark`（429 MiB） |
| 环境变量 | 统一写入 `/etc/profile.d/hadoop-env.sh`（JAVA_HOME / HADOOP_* / YARN_* / SPARK_HOME） |
| HDFS | `hdfs://niyujun01:8020`，`dfs.replication=1`；`/ev-charging/{ods,dwd,dws,ads,quality,forecast}` 已建 |
| 自检结论 | `check_env.sh` **15 项全绿 → 环境自检 PASS** |

### 11.2 验收证据

1. **`jps` 五进程齐全**：NameNode / DataNode / SecondaryNameNode / ResourceManager / NodeManager；
2. **Web UI 可访问**：NameNode `9870`、ResourceManager `8088` 均返回 302（正常重定向）；
3. **MapReduce 冒烟通过**：`grep` 示例作业产出 `1 dfsadmin` / `1 dfs.replication` 等结果，`_SUCCESS` 标记存在（首次耗时约 2 分 47 秒，属 YARN 容器冷启动的正常现象）；
4. **Spark on YARN 打通**：提交 PySpark 作业读取 `hdfs://niyujun01:8020/input/core-site.xml`，输出行数 = 12；YARN 应用列表中该作业为 `FINISHED / SUCCEEDED`。

### 11.3 与其他成员机的差异说明（照做时请注意）

- §5.5 / §5.6 / §6 的环境变量按本记录采用**全局 `/etc/profile.d/hadoop-env.sh`** 方案（原因见 §5.5 说明），其他机器请照此执行，以保证 `check_env.sh` 与跨机脚本行为一致；
- 本机内存仅 7.7 GiB，已按 §5.7(7) 设置 YARN 内存上限 4096 MB，并按 §10.2 限制 Spark executor/driver 为 2g；内存更充裕的机器可忽略；
- **`/etc/hosts` 采用回环写法 `127.0.0.1 localhost niyujun01`（2026-09-15 由 `192.168.59.128 niyujun01` 改）**：
  - 收益 1：`hdfs://niyujun01:8020` 与 `hdfs://localhost:8020` **完全等价**——#3 的作业脚本内部写的是 `localhost:8020`，而本指南要求用主机名，统一后两家脚本可在同一台机器上混跑；
  - 收益 2：虚拟机 DHCP 更换 IP 后无需再同步该行，`hdfs://<主机名>:8020` 不会失联；
  - **注意**：改后必须重启 **HDFS 与 YARN 的全部守护进程**（只重启 HDFS 不够——ResourceManager 会仍绑在旧网卡地址上，导致 `8032 连接被拒绝`，表现为 Spark 提交时反复 `Retrying connect to server: ...:8032`）。
    判断方法：`sudo ss -ltnp | grep :8032`，正常应显示 `127.0.0.1:8032`；若显示 `<网卡IP>:8032` 则 RM 仍是旧进程，需 `stop-yarn.sh` 后强制 `kill` 残留 PID 再 `start-yarn.sh`。
  - Web UI（9870 / 8088）仍绑 `0.0.0.0`，宿主机用 `http://<VM-IP>:9870` 访问不受影响。

### 11.4 集成机运行时入口（`ev-part2`）适配说明

主线 `part2/scripts/` 的作业脚本（`run_pipeline.sh` 等）在第 4 行统一 `source /usr/local/ev-part2/env.sh`，并由 `ev-part2` 命令包装工具链。**该运行时在集成机上采用适配版**：

| 项 | 仓库 `install_global_runtime.sh`（#3 机器用） | 集成机适配版（`niyujun01`，2026-09-15） |
|---|---|---|
| JDK | **JDK 8**（`/usr/local/ev-part2/jdk8`） | **JDK 17**（`/usr/local/jdk-17`，本组冻结基线） |
| Hadoop | **3.2.1**（脚本自带 tarball） | **3.4.1**（既有系统级安装，不覆盖） |
| Python | 自建 CPython 3.10.21 | 系统 **3.10.12**（`/usr/bin/python3`） |
| Spark | 3.5.7 | 3.5.7（同） |
| HDFS/YARN 客户端身份 | 当前用户 | `HADOOP_USER_NAME=hadoop`（本机 Hadoop 由独立 `hadoop` 用户启动） |
| 启停实现 | 按自建配置启动 | 适配版 `cluster.sh` 调既有 `start-dfs.sh`/`start-yarn.sh`，**以守护进程真实数量判活** |

**为什么不直接跑 `install_global_runtime.sh`**：

1. 它会安装 Hadoop 3.2.1 + JDK 8，与本组已冻结的基线（3.4.1 + JDK 17）冲突；
2. 它**显式拒绝覆盖**已存在的 `/usr/local/hadoop`、`/usr/local/spark`（脚本第 20–25 行的保护逻辑），在已装好基线的机器上本就跑不起来；
3. #3 的《local-runtime.md》亦明确要求：「**最终集成前由 #2 选定唯一基线**」「**不用任一安装脚本覆盖另一套已有环境**」。

**集成机的实际安装内容**（仅 3 个文件，不动工具链）：

```text
/usr/local/ev-part2/env.sh          # 适配版：JAVA_HOME=/usr/local/jdk-17；HADOOP_HOME=/usr/local/hadoop(3.4.1)
/usr/local/ev-part2/bin/cluster.sh  # 适配版：以 hadoop 用户启停，按进程数判活
/usr/local/bin/ev-part2             # 包装器（与仓库版一致：start|stop|status|python|hdfs|hadoop|yarn|spark-submit|pyspark|java|jps）
/etc/profile.d/ev-part2.sh          # 指向 env.sh 的软链
```

**验证命令与期望输出**：

```bash
ev-part2 python --version        # → Python 3.10.12
ev-part2 java -version           # → openjdk 17.0.20.1
ev-part2 hadoop version          # → Hadoop 3.4.1
ev-part2 spark-submit --version  # → version 3.5.7
ev-part2 status                  # → 守护进程：5/5
```

> **给其他成员机**：不装 `ev-part2` 时，`run_pipeline.sh` 等脚本会失败在第 4 行。
> 两种做法：① 按上表在本机建同样 3 个文件（把 `JAVA_HOME` 指向本机 JDK 17）；② 在本机 `~/ev-part2/` 下自建 `env.sh` 并 `export EV_PART2_HOME`。
> **不要**把 `install_global_runtime.sh` 直接跑在已装好 3.4.1 基线的机器上——它会因目标已存在而拒绝执行（这是它自身的安全设计，不是故障）。

---

## 12. 附加组件：Node.js（老师要求 Node 23 及以上）

> 老师后续下发要求「nodejs 使用 23 及以上版本，前端使用 vue3」。前端工程 Vue **3.5.13** 已达标，Node 需从 20 升到 23+。

### 12.1 安装方式：官方 tarball（与 JDK/Hadoop/Spark 风格一致，便于离线分发）

```bash
# 1) 下载（清华镜像，约 30 MB）
cd /tmp
curl -LO https://mirrors.tuna.tsinghua.edu.cn/nodejs-release/v23.11.1/node-v23.11.1-linux-x64.tar.xz

# 2) 解压到 /usr/local/node
sudo rm -rf /usr/local/node /usr/local/node-v23.11.1-linux-x64
sudo tar -xJf node-v23.11.1-linux-x64.tar.xz -C /usr/local
sudo mv /usr/local/node-v23.11.1-linux-x64 /usr/local/node
sudo chown -R root:root /usr/local/node

# 3) 软链接（/usr/local/bin 优先级高于 NodeSource 装在 /usr/bin 的旧版）
sudo ln -sf /usr/local/node/bin/node /usr/local/bin/node
sudo ln -sf /usr/local/node/bin/npm  /usr/local/bin/npm
sudo ln -sf /usr/local/node/bin/npx  /usr/local/bin/npx

# 4) 写入全局 PATH（login shell 生效）
echo "export PATH=/usr/local/node/bin:\$PATH" | sudo tee -a /etc/profile.d/hadoop-env.sh

# 5) 验证
node -v    # 期望 v23.11.1
npm -v     # 期望 10.9.2
```

### 12.2 升级后必须复验前端

```bash
cd web
rm -rf node_modules          # Node 大版本变更后必须重装依赖
npm ci --no-audit --no-fund
npm run build                # 期望：✓ built in ~20s，产物 web/dist
```

> 实测（集成机 `niyujun01`，2026-09-15）：Node **v23.11.1** + npm 10.9.2，`npm ci` 65 个包 11 秒、`npm run build` 21 秒成功，**无回归**。

### 12.3 若原来用 NodeSource 安装

NodeSource 会把 `node`/`npm` 装在 `/usr/bin`。本方案装在 `/usr/local/node` 并软链到 `/usr/local/bin`（PATH 优先级更高），**无需卸载旧包**即可切换；如需彻底清理可执行 `sudo apt remove nodejs` 并删除 `/etc/apt/sources.list.d/nodesource.list`。

---

## 13. 数据落 HDFS（老师要求「答辩尽量放到 hadoop 上存储」）

> 老师要求「代码测试过程在本地，**答辩尽量放到 hadoop 上存储（hadoop 3.x 版本）**」。即：数据文件要写进 HDFS，能演示"从 Hadoop 存储读取"，而不是只留在虚拟机本地磁盘。

### 13.1 建业务目录

```bash
sudo -u hadoop bash -lc "hdfs dfs -mkdir -p /ev-charging/{ods,dwd,dws,ads,quality,forecast}"
```

### 13.2 写入数据

ODS/DWS 用 #4 提供的脚本，或用命令行直接上传：

```bash
# 以 hadoop 身份上传（注意：HDFS 里 root 不是超级用户，若用 sudo 执行需声明身份）
sudo bash -lc "source /etc/profile.d/hadoop-env.sh; export HADOOP_USER_NAME=hadoop; \
  hdfs dfs -put -f /home/<用户>/part2/part2/scml/handoff/ods/* /ev-charging/ods/"
```

> **易错点**：`sudo hdfs dfs -put ...` 会报 `Permission denied: user=root, access=WRITE`——HDFS 的超级用户是启动 NameNode 的用户（`hadoop`），root 反而不是。解决方式是 `export HADOOP_USER_NAME=hadoop` 声明身份，或用 `sudo -u hadoop` 执行（但后者要求 hadoop 用户能读源文件）。

### 13.3 读回校验（答辩证据）

```bash
# 目录与容量
sudo -u hadoop bash -lc "hdfs dfs -du -h /ev-charging"

# 分区结构（ODS 保持 dt=YYYY-MM-DD）
sudo -u hadoop bash -lc "hdfs dfs -ls /ev-charging/ods/ods_orders | head -5"

# Spark on YARN 从 HDFS 读回（会留下 YARN 记录）
sudo -u hadoop bash -lc "spark-submit --master yarn --conf spark.sql.shuffle.partitions=8 /tmp/read_ods.py"
```

> 实测（集成机 `niyujun01`，2026-09-15）：ODS 104.5 M、DWS 3.9 M、ADS 5.5 M 已入 HDFS；ODS 的 `ods_orders` 等表保持 `dt=YYYY-MM-DD` 分区结构；Spark on YARN 读回 `ods_stations` 行数 = 8，作业 `FINISHED/SUCCEEDED`。
