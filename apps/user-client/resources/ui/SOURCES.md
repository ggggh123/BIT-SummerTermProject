# 用户端 UI 资源来源

- `login-illustration.png`：OpenAI 内置 ImageGen 于 2026-09-05 根据已批准的 V2 登录视觉稿生成。图片只包含电动汽车、充电桩与浅青绿城市背景，不含界面文字或控件；客户端保持原始宽高比缩放，不拉伸、不裁剪。生成原件：`/home/hushengyuan/.codex/generated_images/01a056bd-a14d-74f0-a6de-34ddaa1f6390/exec-5fbda373-a74d-416e-86fa-54213c69b406.png`。
- `location.svg`、`battery-charging.svg`、`history.svg`、`person.svg`：Google Material Icons（Outlined，24px），取自 `google/material-design-icons` 官方仓库提交 `0cbb08816df07faaae3dca060d4ebb10b66c214f`，对应 `place`、`battery_charging_full`、`history`、`account_circle` 图标。为匹配产品主题，仅将 SVG 根节点填充色设为 `#00856A`，路径数据保持原样。
- `back.svg`、`charger.svg`：同一官方仓库提交的 Material Icons Outlined 24px，分别来自 `src/navigation/arrow_back_ios/materialiconsoutlined/24px.svg` 和 `src/maps/ev_station/materialiconsoutlined/24px.svg`。仅新增根节点填充色 `#00856A`，保留路径原样，与既有图标共用 Apache 2.0 许可。
- `expand-more.svg`：同一提交的 `src/navigation/expand_more/materialiconsoutlined/24px.svg`，用于地址下拉菜单；仅新增根节点填充色 `#61717B`，路径原样、许可相同。

Google Material Icons 采用 Apache License 2.0：

- 官方仓库：https://github.com/google/material-design-icons
- 许可证：https://www.apache.org/licenses/LICENSE-2.0
- 官方说明：https://github.com/google/material-design-icons/blob/master/README.md#license
- 随附完整许可证正文：`LICENSE-APACHE-2.0.txt`
