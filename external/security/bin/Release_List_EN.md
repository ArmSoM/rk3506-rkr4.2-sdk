# Release Note

## optee v2

| Date       | file                                 | Build commit | Severity  |
| ---------- | :----------------------------------- | ------------ | --------- |
| 2024-08-30 | uclibc_lib/arm/libckteec.a/so        | 4cb63d20     | important |

### New

1. uclibc add libckteec libs to fix compile error in rk_tee_usr/.

------

## optee v2

| Date       | file                                 | Build commit | Severity  |
| ---------- | :----------------------------------- | ------------ | --------- |
| 2024-07-22 | libteec.a libckteec.a tee-supplicant | 4cb63d20     | important |

### New

1. Secure storage supports UFS RPMB.

------

## optee v2

| Date       | file                                   | Build commit | Severity  |
| ---------- | :------------------------------------- | ------------ | --------- |
| 2023-05-18 | libckteec.so libteec.so tee-supplicant | 25920641     | important |

### New

1. When using security partitions for secure storage, data is synchronized every time it is written，it can increase secure storage stability, but read and write speeds may slow down.

------

## optee v2

| Date       | file           | Build commit | Severity  |
| ---------- | :------------- | ------------ | --------- |
| 2023-05-18 | uclibc_lib/arm | 25920641     | important |

### New

1. Add OPTEE library which are compiled by the uclibc toolchain.

---

## optee v2

| Date       | file                 | Build commit | Severity |
| ---------- | :------------------- | ------------ | -------- |
| 2023-04-07 | librk_tee_service.so | 0435e966     | moderate |

### New

1. Support read secure boot enable flag.
2. Support read secure boot public key hash.

---
