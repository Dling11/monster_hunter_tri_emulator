FROM monster-hunter-tri-translation:local
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends openjdk-21-jdk-headless cmake ninja-build make perl g++ git unzip pkg-config \
    && rm -rf /var/lib/apt/lists/*
ENV ANDROID_HOME=/opt/android-sdk ANDROID_SDK_ROOT=/opt/android-sdk
ENV ANDROID_NDK_HOME=/opt/android-sdk/ndk/29.0.14206865
ENV ANDROID_NDK_ROOT=/opt/android-sdk/ndk/29.0.14206865
RUN curl -fL --retry 3 https://dl.google.com/android/repository/commandlinetools-linux-15859902_latest.zip -o /tmp/android-tools.zip \
    && echo '4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583  /tmp/android-tools.zip' | sha256sum -c - \
    && mkdir -p /opt/android-sdk/cmdline-tools \
    && unzip -q /tmp/android-tools.zip -d /tmp/android-tools \
    && mv /tmp/android-tools/cmdline-tools /opt/android-sdk/cmdline-tools/latest \
    && rm -rf /tmp/android-tools.zip /tmp/android-tools
RUN yes | /opt/android-sdk/cmdline-tools/latest/bin/sdkmanager --licenses >/dev/null \
    && /opt/android-sdk/cmdline-tools/latest/bin/sdkmanager 'platforms;android-35' 'build-tools;35.0.0' 'ndk;29.0.14206865'
ENV PATH=/opt/android-sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin:/opt/android-sdk/build-tools/35.0.0:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
RUN curl -fL --retry 3 https://github.com/openssl/openssl/releases/download/openssl-3.5.9/openssl-3.5.9.tar.gz -o /tmp/openssl.tar.gz \
    && echo '603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a  /tmp/openssl.tar.gz' | sha256sum -c - \
    && tar -xzf /tmp/openssl.tar.gz -C /tmp \
    && cd /tmp/openssl-3.5.9 \
    && ./Configure android-arm64 -D__ANDROID_API__=29 no-shared no-tests --prefix=/opt/openssl-android --libdir=lib -fPIC \
    && make -j2 >/tmp/openssl-build.log 2>&1 && make install_sw >/tmp/openssl-install.log 2>&1 \
    && rm -rf /tmp/openssl-3.5.9 /tmp/openssl.tar.gz /tmp/openssl-build.log /tmp/openssl-install.log
ENTRYPOINT []
WORKDIR /workspace
