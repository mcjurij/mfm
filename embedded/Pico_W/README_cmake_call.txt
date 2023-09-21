export PICO_SDK_PATH=$HOME/<path to pico sdk>/pico-sdk/

cmake -DPICO_BOARD=pico_w -DWIFI_SSID="<your SSID>" -DWIFI_PASSWORD="<your password>" ..
