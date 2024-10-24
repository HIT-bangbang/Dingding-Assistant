#include <Arduino.h>
#include "BLEDevice.h"
#include "BLEUtils.h"
#include "esp_sleep.h"
#include "BLEBeacon.h"
#define BEACON_UUID           "8ec76ea3-6668-48da-9866-75be8bc86f4d" // UUID 1 

BLEAdvertising *pAdvertising;

uint8_t bleMac[6] = {0x6E, 0x42, 0x4F, 0x0E, 0x6E, 0x61}; // 假设考勤机mac地址为11:22:33:44:55:66
// 0-30 前31组
uint8_t bleRaw[31] = {
    0x02, 0x01, 0x04, 0x03, 0x03, 
    0x3C, 0xFE, 0x17, 0xFF, 0x00, 
    0x01, 0xB5, 0x00, 0x02, 0x0B, 
    0xCF, 0x5E, 0x06, 0x00, 0x00, 
    0x00, 0xBF, 0xD3, 0xCE, 0x67, 
    0x33, 0x01, 0x10, 0x00, 0x00, 0x00
};
// uint8_t bleRaw[] = {
//     0x02, 0x01, 0x06, 0x03, 0x03, 0x3C, 0xFE, 0x17, 0xFF, 0x00, 0x02, 0x72, 0x00, 0x1F, 0x71, 0x5A, 0x4B, 0xA6, 0xBA, 0xB5, 0x00, 0xC4, 0xF8, 0xA7, 0x63, 0x9E, 0xCA, 0xCC, 0x00, 0x02, 0x20, 
// }; 
// 去除复制得到的文本最前面的0x后，两两一组填入，由于数据很长，我们可以写一个简单的脚本完成这个工作
// 如果复制出来的raw超过31组 那么把它改为true并维护下面的数组
boolean rawMoreThan31 = false;
// 31-end
uint8_t bleRaw32[] = {0x0C,0x09,0x52,0x54,0x4B,0x5F,0x42,0x54,0x5F,0x34,0x2E,0x31,0x00};

#define SERVICE_UUID "0000FE3C-0000-1000-8000-00805F9B34FB"
#define CHARACTERISTIC_ONE_UUID "0000FE1B-0000-1000-8000-00805F9B34FB"
#define CHARACTERISTIC_TWO_UUID "0000FE1C-0000-1000-8000-00805F9B34FB"

// 回调函数，用于处理特性通知
class MyCallbacks : public BLECharacteristicCallbacks {
  void onRead(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    Serial.print("Characteristic ");
    Serial.print(pCharacteristic->getUUID().toString().c_str());
    Serial.print(" read: ");
    Serial.println(value.c_str());
  }

  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    Serial.print("Characteristic ");
    Serial.print(pCharacteristic->getUUID().toString().c_str());
    Serial.print(" write: ");
    Serial.println(value.c_str());
  }

  void onNotify(BLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    Serial.print("Characteristic ");
    Serial.print(pCharacteristic->getUUID().toString().c_str());
    Serial.print(" notify: ");
    Serial.println(value.c_str());
  }
};

void setup() {
  Serial.begin(115200); // 初始化串口
  delay(2000);

  // esp32没有提供设置蓝牙mac地址的api 通过查看esp32的源代码
  // 此操作将根据蓝牙mac算出base mac
  if (UNIVERSAL_MAC_ADDR_NUM == FOUR_UNIVERSAL_MAC_ADDR) {
    bleMac[5] -= 2;
  } else if (UNIVERSAL_MAC_ADDR_NUM == TWO_UNIVERSAL_MAC_ADDR) {
    bleMac[5] -= 1;
  }
  esp_base_mac_addr_set(bleMac);

  // 初始化
  BLEDevice::init("");
  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer(); // <-- no longer required to instantiate BLEServer, less flash and ram usage

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);
  // Create a BLE Characteristic
  BLECharacteristic *pCharacteristic_one = pService->createCharacteristic(
                                          CHARACTERISTIC_ONE_UUID,
                                          BLECharacteristic::PROPERTY_READ |
                                          BLECharacteristic::PROPERTY_NOTIFY
                                        );

  BLECharacteristic *pCharacteristic_two = pService->createCharacteristic(
                                          CHARACTERISTIC_TWO_UUID,
                                          BLECharacteristic::PROPERTY_WRITE
                                        );

  // 设置回调函数
  pCharacteristic_two->setCallbacks(new MyCallbacks());
  pCharacteristic_one->setCallbacks(new MyCallbacks());

  // Start the service
  pService->start();
  pAdvertising = pServer->getAdvertising();

  BLEAdvertisementData oScanResponseData = BLEAdvertisementData();
  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();
  // 设备信息设置成空白的
  pAdvertising->setScanResponseData(oScanResponseData);

    // 有些参考资料在这里会直接发 `pAdvertising->setAdvertisementData(oAdvertisementData);`
    // 但是经过我测试，oAdvertisementData 不设置Data的话，会使esp32设备运行时报错并重启
    // 所以 在这里就给 oAdvertisementData 随意设置一些数据吧
  BLEBeacon oBeacon = BLEBeacon();
  oBeacon.setManufacturerId(0x4C00);                // fake Apple 0x004C LSB (ENDIAN_CHANGE_U16!)
  oBeacon.setProximityUUID(BLEUUID(BEACON_UUID));
  oBeacon.setMajor(0x007B);
  oBeacon.setMinor(0x01C8);

  oAdvertisementData.setFlags(0x04);    // BR_EDR_NOT_SUPPORTED 0x04
  std::string strServiceData = "";
  strServiceData += (char)26;           // Len
  strServiceData += (char)0xFF;         // Type
  strServiceData += oBeacon.getData(); 
  oAdvertisementData.addData(strServiceData);

  pAdvertising->setAdvertisementData(oAdvertisementData);


  // 简单粗暴直接底层api重新设置一下抓到的raw
  esp_err_t errRc = ::esp_ble_gap_config_adv_data_raw((uint8_t *)bleRaw, sizeof(bleRaw));
  if (errRc != ESP_OK) {
    Serial.printf("esp_ble_gap_config_adv_data_raw: %d\n", errRc);
  }
  // 超过31
  if (rawMoreThan31) {
    errRc = ::esp_ble_gap_config_scan_rsp_data_raw(bleRaw32, sizeof(bleRaw32)/sizeof(bleRaw32[0]));
    if (errRc != ESP_OK) {
      Serial.printf("esp_ble_gap_config_scan_rsp_data_raw: %d\n", errRc);
    }
  }

  pAdvertising->start();
}

void loop() {
  // 闪灯灯 至于为什么是串口输出，因为并没有内置led，但拥有串口指示灯
  Serial.println("Sparkle");
  delay(1000);
  // 20分钟去待机避免忘了关
  if (millis() > 1200000) {
    esp_deep_sleep_start();
  }
}