# 电控组第三次培训作业

姓名：徐振轩
学号：20263003843

## 一、参考资料与驱动来源

### 1. 培训资料

- 《第三次培训作业：串口和按键控制流水灯与OLED状态显示》
- 《第三次培训学习指南》
- `第三次培训所用资料.zip`（含 OLED 驱动、串口助手 V1.1）

### 2. 视频教程

| 学习内容 | 参考来源 |
|---|---|
| GPIO 输入、按键电平与上拉 | Keysking 第 4、5 集
| 外部中断 EXTI 与中断处理流程 | Keysking 第 6、7 集
| 串口轮询发送与中断收发 | Keysking 第 8、9 集
| 串口数据包与 HEX 收发 | Keysking 第 11 集
| 软件 I²C 与 OLED 显示 | Keysking 第 12 集
| DMA 发送（仅了解，未使用） | Keysking 第 10 集

### 3. 驱动与库

- **OLED 驱动**：使用培训资料中提供的 `OLED.c`、`OLED.h`、`OLED_Font.h`
  - 软件 I²C，SCL = PB1，SDA = PB0，从机地址 0x78
  - 8×16 字模，可显示 4 行、每行 16 个字符
  - 未修改驱动源码，仅在 `App_Init()` 中调用 `OLED_Init()` 及显示函数
- **STM32 HAL 库**：STM32Cube FW_F1 V1.8.7
  - 工程由 STM32CubeMX 6.15.0 生成
  - `stm32f1xx_hal_uart.c` / `stm32f1xx_hal_uart.h` 从该固件包复制到工程
    `Drivers/STM32F1xx_HAL_Driver/` 目录
- **串口调试工具**：B站keysking提供 地址：serial.keysking.com

### 4. 自己实现的部分

- `Core/Src/app.c`、`Core/Inc/app.h`
  - 按键外部中断与 30ms 软件消抖
  - 串口单字节接收中断与 6 字节组包状态机
  - 数据包校验和计算与比较
  - 流水灯非阻塞计时、方向切换与暂停/继续
  - OLED 状态刷新
  - 串口中断发送与待发标志处理
- `Core/Src/main.c`：仅保留外设初始化调用与主循环调用

## 二、串口反馈格式

### 1. 格式定义

采用**单行 ASCII 文本**，以 `\r\n` 结尾，字段之间用英文逗号分隔：

```text
STATE=<运行状态>,DIR=<方向>,STEP=<切换间隔>,LED=<当前LED编号>\r\n
```

| 字段 | 取值 | 说明 |
|---|---|---|
| `STATE` | `RUN` / `PAUSE` | 运行 / 暂停 |
| `DIR` | `FWD` / `REV` | 正向 / 反向 |
| `STEP` | 100～2000 | 相邻 LED 切换间隔，单位 ms，十进制 |
| `LED` | 1～3 | 当前点亮的 LED 编号 |

### 2. 发送时机

- 串口数据包校验通过并应用后发送一次
- 按键改变运行状态或方向后发送一次
- 不随每次切灯发送，避免刷屏
- 使用 `HAL_UART_Transmit_IT()` 中断发送，不阻塞主循环
- 若上一包尚未发完又产生新事件，先记录待发标志，等发送完成回调后补发最新状态

### 3. 串口参数

| 参数 | 值 |
|---|---|
| 波特率 | 115200 |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验位 | 无 |
| 串口助手接收模式 | 文本模式，GBK |

### 4. 选择文本格式的原因

- 串口助手直接可读，便于调试
- 字段固定、顺序固定，解析和人工核对简单

### 5. 反馈生成代码

```c
n = snprintf(tx_msg, sizeof(tx_msg),
             "STATE=%s,DIR=%s,STEP=%u,LED=%u\r\n",
             st.run ? "RUN" : "PAUSE",
             st.dir ? "REV" : "FWD",
             (unsigned)st.step,
             (unsigned)(st.led + 1));
tx_busy = 1;
HAL_UART_Transmit_IT(&huart1, (uint8_t *)tx_msg, (uint16_t)n);
```
