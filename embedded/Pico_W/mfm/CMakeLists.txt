
add_executable(mfm_pico_w
  main.c
  freq.c
  ntime.c
  test_tcp_client.c
  tcp_client.c
  proto.c
  ringbuffer.c
)


target_link_libraries(mfm_pico_w pico_stdlib pico_multicore pico_cyw43_arch_lwip_threadsafe_background)

target_compile_definitions(mfm_pico_w PRIVATE
  WIFI_SSID=\"${WIFI_SSID}\"
  WIFI_PASSWORD=\"${WIFI_PASSWORD}\"
  TCP_SERVER_IP=\"192.168.178.59\"
)

target_include_directories(mfm_pico_w PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
#  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
)

pico_enable_stdio_usb(mfm_pico_w 1)
pico_enable_stdio_uart(mfm_pico_w 0)


# create map/bin/hex file etc.
pico_add_extra_outputs(mfm_pico_w)
