# Release Note

## optee v2

| 时间       | 文件                                 | 编译 commit | 重要程度 |
| ---------- | :----------------------------------- | ----------- | -------- |
| 2024-08-30 | uclibc_lib/arm/libckteec.a/so        | 4cb63d20    | 重要     |

### New

1. uclibc增加libckteec相关库，解决rk_tee_user仓库xtest编译报错问题。

------

## optee v2

| 时间       | 文件                                 | 编译 commit | 重要程度 |
| ---------- | :----------------------------------- | ----------- | -------- |
| 2024-07-22 | libteec.a libckteec.a tee-supplicant | 4cb63d20    | 重要     |

### New

1. 安全存储支持 UFS RPMB。

------

## optee v2

| 时间       | 文件                                   | 编译 commit | 重要程度 |
| ---------- | :------------------------------------- | ----------- | -------- |
| 2023-05-18 | libckteec.so libteec.so tee-supplicant | 25920641    | 重要     |

### New

1. 安全存储使用security分区时，每次写入都会同步数据，可以增加安全存储稳定性，但读写速度会变慢。

------

## optee v2

| 时间       | 文件           | 编译 commit | 重要程度 |
| ---------- | :------------- | ----------- | -------- |
| 2023-05-18 | uclibc_lib/arm | 25920641    | 重要     |

### New

1. 新增uclibc编译器编译的OPTEE库文件。

---

## optee v2

| 时间       | 文件                 | 编译 commit | 重要程度 |
| ---------- | :------------------- | ----------- | -------- |
| 2023-04-07 | librk_tee_service.so | 0435e966    | 普通     |

### New

1. 添加读取安全启动使能标志接口。
2. 添加读取安全启动公钥哈希接口。

---
