ServiceSync Embedded Firmware App
===

このアプリケーションは、ServiceSync Embedded Gatewayで動作するファームウェア更新のアプリケーションです。


## ビルド手順

### プラットフォーム証明書（moat.pem）の配置

ゲートウェイパッケージに署名を行うために、プラットフォーム証明書（moat.pem）が必要です。プラットフォーム証明書はServiceSyncサーバー（DMS）毎に異なります。事前にシステム管理者より入手してください。

moat.pemをssegw-file/certsにコピーしてください。

```
$ cd /path/to/ssegw-firmware
$ cp /path/to/moat.pem ./certs
```

### `token.bin`の生成

ゲートウェイパッケージに署名をするために必要なトークン（token.bin）を取得します。取得方法は以下の通りです。

```
$ cd package
$ export SSDMS_PREFIX="YOUR SERVICESYNC DMS URL"
$ export APP_ID="YOUR PP's APPLICATION ID"
$ export CLIENT_ID="YOUR PP's CLIENT ID"
$ export CLIENT_SECRET="YOUR PP's PASSWORD"
$ export PACKAGE_ID="firmware"
$ export TOKEN=`curl -v "${SSDMS_PREFIX}/moat/v1/sys/auth?a=${APP_ID}&u=${CLIENT_ID}&c=${CLIENT_SECRET}" | sed 's/\\\\\//\//g' | sed 's/[{}]//g' | awk -v k="text" '{n=split($0,a,","); for (i=1; i<=n; i++) print a[i]}' | sed 's/\"\:\"/\|/g' | sed 's/[\,]/ /g' | sed 's/\"//g' | grep -w 'accessToken' | cut -d"|" -f2 | sed -e 's/^ *//g' -e 's/ *$//g'`
$ curl -v -o token.bin -L "${SSDMS_PREFIX}/moat/v1/sys/package/${PACKAGE_ID}?token=${TOKEN}&secureToken=true"
$ cd ..
```

### ビルド

パッケージのビルド方法はゲートウェイ製品毎に異なります。以下の手順に従ってパッケージをビルドしてください。 `firmware_<VERSION>_<ARCH>_<PRODUCT>.zip` という名前のファイルが生成されます。

#### 標準的なインテルPC

```
debian$ ./configure
debian$ make package
```

#### Armadillo-IoT

```
atde5$ export CROSS=arm-linux-gnueabi-
atde5$ export CC=${CROSS}gcc
atde5$ export CXX=${CROSS}g++
atde5$ export AR=${CROSS}ar
atde5$ export LD=${CROSS}ld
atde5$ export RANLIB=${CROSS}ranlib
atde5$ export STRIP=${CROSS}strip
atde5$ ./configure --dest-cpu=arm --product=Armadillo-IoT
atde5$ make package
```

## 変更履歴

Changes in `1.0.0` : 2015/11/30

* Initial Release.
