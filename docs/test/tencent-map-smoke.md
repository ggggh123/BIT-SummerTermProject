# 腾讯地图导航人工冒烟记录

状态：**PENDING**

本资源合同的自动化测试不会访问腾讯服务，也不会提交或记录腾讯地图 Key。仍需在本机以有效、未提交的 Key 启动真实 Qt QWebEngine 后完成以下检查：

1. 以固定的本地 `qrc:/map/navigation.html` 加载页面并调用 `configureMap`。
2. 输入固定起点和站点终点，验证驾车路线显示；切换到步行后验证路线更新。
3. 断开网络或令路线请求失败，验证中文错误、重试按钮和“上次成功路线”说明均可见。

官方 JavaScript API GL 文档使用 `https://map.qq.com/api/gljs?v=1.exp`、服务库和回调加载方式，并以 Driving/Walking 服务规划路线。腾讯 FAQ 明确将 `file://` 列为不支持来源；官方资料未明确说明 `qrc:` 来源是否受支持。因此本项尚未通过：后续真实 QWebEngine 冒烟必须判定固定 qrc 加载是否可用；若不可用，需评审改用 localhost 来源的架构，不能在此记录为腾讯在线成功。

参考：<https://lbs.qq.com/webApi/javascriptGL/glGuide/glBasic>、<https://lbs.qq.com/webApi/javascriptGL/glDoc/glDocService>、<https://lbs.qq.com/webApi/javascriptGL/glGuide/service>、<https://lbs.qq.com/faq/webFaq/jsApiGl>。
