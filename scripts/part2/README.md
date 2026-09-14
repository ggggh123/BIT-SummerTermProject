# 第二阶段 Ubuntu 环境：体检与补齐

> 用途：第二阶段（Hadoop + PySpark + SparkSQL + Flask/Vue 大屏 + Spark MLlib）在本组的 Ubuntu 虚拟机上跑通所需环境的检查与安装命令。
> 口径：以老师《2026_09_12_00.先看这里！！！》材料为准 —— 完整环境镜像为 **Hadoop 3.3.0 + Spark 3.4.1，装在 `/opt/module/`**，`start-all.sh` 一键起 HDFS+YARN，HDFS RPC 端口 **9000**。

## 最小必装清单

| 档位 | 项目 | 理由 |
|---|---|---|
| 必装 | **Spark 3.4.1** | 老师镜像口径；PySpark / SparkSQL / MLlib 全靠它 |
| 必装 | **Python 3.11 解释器 + pyspark** | 系统 Python 3.13 与 PySpark 3.5 不兼容；PySpark 作业必须在 VM 里跑（宿主机是 Windows Python，驱不动 Hadoop 上的 Spark） |
| 按需 | `flask`、`flask-cors` | 只有 Flask 也在 VM 里跑才装 |
| 按需 | `pandas`、`pyarrow` | 只在需要直接核对 ADS Parquet 数值时方便，可省 |
| 按需 | Node 20 | 只在 VM 里跑 `npm run dev` 才要；在 Windows 上 `npm run build` 出 dist 拷进来就不用装 |
| 不用装 | PyCharm | 用 VM 里已有的 CodeBuddy（VS Code 内核）写 PySpark 足够；仅当老师当面点验步骤第 2 条，再补第 4 节那条命令 |
| 不用装 | HBuilderX | 老师给的前端 IDE，走 Vite 路线不需要 |
| 不用装 | MySQL / Hive | 老师明确"数据库可用第一阶段 SQLite"；数仓用 Spark catalog + Parquet 即可 |

## 0. 先做体检

```bash
bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-check.sh
```

输出末尾会给「缺失清单」。下面是各缺失项的补齐命令。

把结果落盘回传给宿主机（Windows 端路径 `D:\BIT-SummerTermProject\scripts\part2\env-report.txt`）：

```bash
bash /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-check.sh | tee /mnt/hgfs/BIT-SummerTermProject/scripts/part2/env-report.txt
```

## 0.1 如果 apt 报 404（Ubuntu 25.04）

25.04 是过渡版本（支持到 2026-01），官方源已下架，`apt-get update` 会全部 404。改用归档源：

```bash
# 25.04 用 deb822 格式的源文件；老版本才是 /etc/apt/sources.list
if [ -f /etc/apt/sources.list.d/ubuntu.sources ]; then
  sudo sed -i -E 's|https?://[^ ]*ubuntu\.com/ubuntu|https://old-releases.ubuntu.com/ubuntu|g' /etc/apt/sources.list.d/ubuntu.sources
else
  sudo sed -i -E 's|https?://[^ ]*ubuntu\.com/ubuntu|https://old-releases.ubuntu.com/ubuntu|g' /etc/apt/sources.list
fi
sudo apt-get update
```

若源里用的是非 `ubuntu.com` 的国内镜像（如 tuna、aliyun），需要手工把地址也改成 `old-releases.ubuntu.com`。

## 1. Spark 3.4.1（对齐老师镜像，必装）

VM 能联网：从 Apache 归档下载；不能联网：把 `spark-3.4.1-bin-hadoop3.tgz` 放到宿主机的 `D:\BIT-SummerTermProject\`，在 VM 里从 `/mnt/hgfs/BIT-SummerTermProject/` 取。

```bash
mkdir -p /opt/software /opt/module && cd /opt/software
# 有网时：
sudo curl -O https://archive.apache.org/dist/spark/spark-3.4.1/spark-3.4.1-bin-hadoop3.tgz
# 或者从共享文件夹拷贝（宿主机先下好放进去）：
# sudo cp /mnt/hgfs/BIT-SummerTermProject/spark-3.4.1-bin-hadoop3.tgz /opt/software/

sudo tar -zxvf spark-3.4.1-bin-hadoop3.tgz -C /opt/module
sudo mv /opt/module/spark-3.4.1-bin-hadoop3 /opt/module/spark-3.4.1
sudo chown -R "$USER":"$USER" /opt/module/spark-3.4.1

cat >> ~/.bashrc <<'EOF'
export SPARK_HOME=/opt/module/spark-3.4.1
export PATH=$SPARK_HOME/bin:$SPARK_HOME/sbin:$PATH
export HADOOP_CONF_DIR=/opt/module/hadoop-3.3.0/etc/hadoop
EOF
source ~/.bashrc

# 让 Spark 读到 HDFS 配置（HADOOP_CONF_DIR 指对就不用拷；不确定就拷一份）
cp $HADOOP_CONF_DIR/core-site.xml $HADOOP_CONF_DIR/hdfs-site.xml $SPARK_HOME/conf/ 2>/dev/null
```

Hadoop 装在别处时，把上面的 `hadoop-3.3.0` 换成实际目录（体检脚本会打印发现的目录）。

验证：

```bash
spark-submit --version
pyspark --master yarn
>>> spark.read.text("hdfs://localhost:9000/README.txt").count()   # 换成 HDFS 上真实存在的文件
```

## 2. Python 3.11 + PySpark（必装）

**不要用系统 Python。** Ubuntu 25.04 自带 Python 3.13，PySpark 3.4/3.5 官方只支持 3.8–3.11，导入就会炸。用 uv 或 conda 建一个 3.11 环境。

```bash
# 方案 A：uv（推荐，装完即用）
curl -LsSf https://astral.sh/uv/install.sh | sh
source ~/.bashrc
uv venv --python 3.11 /opt/module/venv311
source /opt/module/venv311/bin/activate
uv pip install pyspark==3.5.3 pandas pyarrow flask flask-cors

# 方案 B：conda 存在时
# conda create -y -n spark python=3.11 && conda activate spark
# pip install pyspark==3.5.3 pandas pyarrow flask flask-cors
```

装上 `SPARK_HOME` 后 `pyspark` 用哪个 Python 由 `PYSPARK_PYTHON` 决定：

```bash
cat >> ~/.bashrc <<'EOF'
export PYSPARK_PYTHON=/opt/module/venv311/bin/python
export PYSPARK_DRIVER_PYTHON=/opt/module/venv311/bin/python
EOF
source ~/.bashrc
python -c "import pyspark; print(pyspark.__version__)"
```

离线环境下用 `uv pip install --find-links=/mnt/hgfs/BIT-SummerTermProject/wheels ...`，wheels 目录在能联网的机器上 `pip download` 出来。

## 3. Node 20（按需：只在 VM 里跑前端 dev server 才装）

老师另外给了 **HBuilderX**（`01.单机版Hadoop安装包/HBuilderX.5.24...zip`），那是他预期的前端 IDE，做 uni-app/Vue 无需自己配 Node。若你走 Vite 路线：

```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt-get install -y nodejs
node -v && npm -v
```

离线时打包 `node_modules`（联网机 `npm install` 后整体拷进共享文件夹），在工程目录 `npm ci --offline`。

## 4. PyCharm（按需，默认不装）

老师给的 Python 安装包是 **Windows 版**，说明他预期 IDE 也可能装在宿主机（用 FinalShell 连 VM，或直接本地写脚本）。两条路都行：

```bash
# VM 内（snap）
sudo snap install pycharm-community --classic
# 或下载 tar.gz 解压到 /opt（离线友好）
# sudo tar -zxvf pycharm-community-*.tar.gz -C /opt && /opt/pycharm-*/bin/pycharm.sh
```

PyCharm 里把解释器指到 `/opt/module/venv311/bin/python`，Run Configuration 里补 `PYTHONPATH`/`SPARK_HOME`，就能在本机跑 PySpark 作业。

## 5. 北京 GeoJSON（大屏地图必需，走本地文件）

不能走 CDN，VM 也可能没网。在能联网的机器上下载后放进仓库，由前端本地引用：

```bash
curl -o beijing.json "https://geo.datav.aliyun.com/areas_v3/bound/110000_full.json"
```

放到前端工程的 `public/geo/beijing.json`（或 `src/assets/geo/`），ECharts `registerMap('beijing', geoJson)` 使用。

## 6. HDFS 业务目录 + 起停顺序

```bash
hdfs dfs -mkdir -p /ev-charging/{ods,dwd,dws,ads,quality,forecast}

# 起：HDFS + YARN → Spark → 作业 → Flask
start-all.sh          # 老师镜像用这个；分开起用 start-dfs.sh + start-yarn.sh
jps                   # 期望 NameNode/DataNode/SecondaryNameNode/ResourceManager/NodeManager
```

写入失败先查安全模式：`hdfs dfsadmin -safemode get`，必要时 `hdfs dfsadmin -safemode leave`。

## 7. 常见坑

| 现象 | 原因 | 处理 |
|---|---|---|
| `apt-get update` 全 404 | Ubuntu 25.04 已 EOL | 见第 0.1 节换 old-releases |
| `import pyspark` 报错 / 段错误 | 系统 Python 3.13 与 PySpark 3.5 不兼容 | 见第 2 节建 3.11 环境并设 `PYSPARK_PYTHON` |
| Spark 读不到 HDFS | `HADOOP_CONF_DIR` 没设或没拷 `core-site.xml` | 见第 1 节 |
| 作业 OOM / 反复失败 | 6 GB 内存偏小 | VM 内存调到 8–12 GB；`--executor-memory 2g --driver-memory 2g`；`spark.sql.shuffle.partitions=8`；必要时 `yarn.nodemanager.resource.memory-mb=4096` |
| 脚本报 `$'\r': command not found` | 文件被 Windows 存成 CRLF | `sed -i 's/\r$//' scripts/part2/env-check.sh` |
