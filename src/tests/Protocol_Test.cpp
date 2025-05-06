#include <Protocol.h>
#include <gtest/gtest.h>

// Test for UvtUtils::calculateChecksum
TEST(UvtUtilsTest, CalculateChecksum) {
  uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t expectedChecksum = 0x04; // Example expected checksum
  uint8_t calculatedChecksum = UvtUtils::calculateChecksum(data, sizeof(data));
  EXPECT_EQ(calculatedChecksum, expectedChecksum);
}

// Test for UvtUtils::validateChecksum
TEST(UvtUtilsTest, ValidateChecksum) {
  uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t checksum = UvtUtils::calculateChecksum(data, sizeof(data));
  EXPECT_TRUE(UvtUtils::validateChecksum(data, sizeof(data), checksum));
}

// Test for UvtUtils::AppendMsgBind
TEST(UvtUtilsTest, AppendMsgBind) {
  MsgBind bind = {12345}; // Example clientId
  std::vector<uint8_t> outputBuffer;

  UvtUtils::AppendMsgBind(bind, outputBuffer);

  // Check the size of the output buffer
  EXPECT_EQ(outputBuffer.size(), sizeof(MsgBind));

  // Check the content of the output buffer
  MsgBind *bindPtr = reinterpret_cast<MsgBind *>(outputBuffer.data());
  EXPECT_EQ(bindPtr->clientId, bind.clientId);
}

// Test for UvtUtils::ExtractMsgBind
TEST(UvtUtilsTest, ExtractMsgBind) {
  MsgBind originalBind = {12345}; // Example clientId
  std::vector<uint8_t> data(sizeof(MsgBind));
  std::memcpy(data.data(), &originalBind, sizeof(MsgBind));

  MsgBind extractedBind;
  bool result = UvtUtils::ExtractMsgBind(data, extractedBind);

  EXPECT_TRUE(result);
  EXPECT_EQ(extractedBind.clientId, originalBind.clientId);
}

TEST(UvtUtilsTest, AppendUdpData) {
  std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
  uint8_t id = 1;
  std::vector<uint8_t> outputBuffer;

  UvtUtils::AppendUdpData(data, id, outputBuffer);

  // Check the size of the output buffer
  EXPECT_EQ(outputBuffer.size(), HEADER_SIZE + data.size());

  // Check the header
  UvtHeader *header = reinterpret_cast<UvtHeader *>(outputBuffer.data());
  EXPECT_EQ(header->size, ntohs(static_cast<uint16_t>(data.size())));
  EXPECT_EQ(header->id, id);
  EXPECT_EQ(header->checksum,
            UvtUtils::calculateChecksum(data.data(), data.size()));
}

// Test for UvtUtils::AppendUdpData with empty data
TEST(UvtUtilsTest, AppendUdpDataEmpty) {
  std::vector<uint8_t> data;
  uint8_t id = 1;
  std::vector<uint8_t> outputBuffer;

  UvtUtils::AppendUdpData(data, id, outputBuffer);

  // Check the size of the output buffer
  EXPECT_EQ(outputBuffer.size(), HEADER_SIZE);

  // Check the header
  UvtHeader *header = reinterpret_cast<UvtHeader *>(outputBuffer.data());
  EXPECT_EQ(header->size, static_cast<uint16_t>(data.size()));
  EXPECT_EQ(header->id, id);
  EXPECT_EQ(header->checksum,
            UvtUtils::calculateChecksum(data.data(), data.size()));
}

// Test for UvtUtils::AppendUdpData with std::vector<char>
TEST(UvtUtilsTest, AppendUdpDataChar) {
  std::vector<char> data = {'a', 'b', 'c', 'd'};
  uint8_t id = 1;
  std::vector<char> outputBuffer;

  UvtUtils::AppendUdpData(data, id, outputBuffer);

  // Check the size of the output buffer
  EXPECT_EQ(outputBuffer.size(), HEADER_SIZE + data.size());

  // Check the header
  UvtHeader *header = reinterpret_cast<UvtHeader *>(outputBuffer.data());
  EXPECT_EQ(header->size, htons(static_cast<uint16_t>(data.size())));
  EXPECT_EQ(header->id, id);
  EXPECT_EQ(header->checksum,
            UvtUtils::calculateChecksum(
                reinterpret_cast<const uint8_t *>(data.data()), data.size()));
}

// Test for UvtUtils::AppendUdpData with empty std::vector<char>
TEST(UvtUtilsTest, AppendUdpDataEmptyChar) {
  std::vector<char> data;
  uint8_t id = 1;
  std::vector<char> outputBuffer;

  UvtUtils::AppendUdpData(data, id, outputBuffer);

  // Check the size of the output buffer
  EXPECT_EQ(outputBuffer.size(), HEADER_SIZE);

  // Check the header
  UvtHeader *header = reinterpret_cast<UvtHeader *>(outputBuffer.data());
  EXPECT_EQ(header->size, static_cast<uint16_t>(data.size()));
  EXPECT_EQ(header->id, id);
  EXPECT_EQ(header->checksum,
            UvtUtils::calculateChecksum(
                reinterpret_cast<const uint8_t *>(data.data()), data.size()));
}

// Test for UvtUtils::AppendUdpData with large std::vector<char>
TEST(UvtUtilsTest, AppendUdpDataLargeChar) {
  std::vector<char> data(1000, 'a'); // 1000 bytes of data
  uint8_t id = 1;
  std::vector<char> outputBuffer;

  UvtUtils::AppendUdpData(data, id, outputBuffer);

  // Check the size of the output buffer
  EXPECT_EQ(outputBuffer.size(), HEADER_SIZE + data.size());

  // Check the header
  UvtHeader *header = reinterpret_cast<UvtHeader *>(outputBuffer.data());
  EXPECT_EQ(header->size, htons(static_cast<uint16_t>(data.size())));
  EXPECT_EQ(header->id, id);
  EXPECT_EQ(header->checksum,
            UvtUtils::calculateChecksum(
                reinterpret_cast<const uint8_t *>(data.data()), data.size()));
}

// Test for UvtUtils::AppendUdpData with multiple calls for std::vector<char>
TEST(UvtUtilsTest, AppendUdpDataMultipleCallsChar) {
  std::vector<char> data1 = {'a', 'b', 'c', 'd'};
  std::vector<char> data2 = {'e', 'f', 'g', 'h'};
  uint8_t id = 1;
  std::vector<char> outputBuffer;

  UvtUtils::AppendUdpData(data1, id, outputBuffer);
  UvtUtils::AppendUdpData(data2, id, outputBuffer);

  // Check the size of the output buffer
  EXPECT_EQ(outputBuffer.size(),
            HEADER_SIZE + data1.size() + HEADER_SIZE + data2.size());

  // Check the first header
  UvtHeader *header1 = reinterpret_cast<UvtHeader *>(outputBuffer.data());
  EXPECT_EQ(header1->size, htons(static_cast<uint16_t>(data1.size())));
  EXPECT_EQ(header1->id, id);
  EXPECT_EQ(header1->checksum,
            UvtUtils::calculateChecksum(
                reinterpret_cast<const uint8_t *>(data1.data()), data1.size()));

  // Check the second header
  UvtHeader *header2 = reinterpret_cast<UvtHeader *>(
      outputBuffer.data() + HEADER_SIZE + data1.size());
  EXPECT_EQ(header2->size, htons(static_cast<uint16_t>(data2.size())));
  EXPECT_EQ(header2->id, id);
  EXPECT_EQ(header2->checksum,
            UvtUtils::calculateChecksum(
                reinterpret_cast<const uint8_t *>(data2.data()), data2.size()));
}

// Test for UvtUtils::AppendUdpData with large data
TEST(UvtUtilsTest, AppendUdpDataLarge) {
  std::vector<uint8_t> data(1000, 0x01); // 1000 bytes of data
  uint8_t id = 1;
  std::vector<uint8_t> outputBuffer;

  UvtUtils::AppendUdpData(data, id, outputBuffer);

  // Check the size of the output buffer
  EXPECT_EQ(outputBuffer.size(), HEADER_SIZE + data.size());

  // Check the header
  UvtHeader *header = reinterpret_cast<UvtHeader *>(outputBuffer.data());
  EXPECT_EQ(header->size, htons(static_cast<uint16_t>(data.size())));
  EXPECT_EQ(header->id, id);
  EXPECT_EQ(header->checksum,
            UvtUtils::calculateChecksum(data.data(), data.size()));
}

// Test for UvtUtils::AppendUdpData with multiple calls
TEST(UvtUtilsTest, AppendUdpDataMultipleCalls) {
  std::vector<uint8_t> data1 = {0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> data2 = {0x05, 0x06, 0x07, 0x08};
  uint8_t id = 1;
  std::vector<uint8_t> outputBuffer;

  UvtUtils::AppendUdpData(data1, id, outputBuffer);
  UvtUtils::AppendUdpData(data2, id, outputBuffer);

  // Check the size of the output buffer
  EXPECT_EQ(outputBuffer.size(),
            HEADER_SIZE + data1.size() + HEADER_SIZE + data2.size());

  // Check the first header
  UvtHeader *header1 = reinterpret_cast<UvtHeader *>(outputBuffer.data());
  EXPECT_EQ(header1->size, htons(static_cast<uint16_t>(data1.size())));
  EXPECT_EQ(header1->id, id);
  EXPECT_EQ(header1->checksum,
            UvtUtils::calculateChecksum(data1.data(), data1.size()));

  // Check the second header
  UvtHeader *header2 = reinterpret_cast<UvtHeader *>(
      outputBuffer.data() + HEADER_SIZE + data1.size());
  EXPECT_EQ(header2->size, htons(static_cast<uint16_t>(data2.size())));
  EXPECT_EQ(header2->id, id);
  EXPECT_EQ(header2->checksum,
            UvtUtils::calculateChecksum(data2.data(), data2.size()));
}

TEST(UvtUtilsTest, ExtractUdpDataWithEnoughData) {
  std::vector<uint8_t> originalData = {0x01, 0x02, 0x03, 0x04};

  std::vector<uint8_t> EncodedData;
  EXPECT_NO_THROW({ UvtUtils::AppendUdpData(originalData, 1, EncodedData); });

  std::vector<uint8_t> DecodedData;
  EXPECT_NO_THROW({
    auto restData = UvtUtils::ExtractUdpData(EncodedData, DecodedData);
    EXPECT_EQ(DecodedData.size(), originalData.size());
    EXPECT_EQ(DecodedData, originalData);
    EXPECT_EQ(restData.size(), 0); // All data should be extracted
  });
}

TEST(UvtUtilsTest, ExtractUdpDataWithoutEnoughData) {
  std::vector<uint8_t> originalData = {0x01, 0x02, 0x03, 0x04};

  std::vector<uint8_t> extraData = {0x05, 0x06};

  std::vector<uint8_t> EncodedData;

  EXPECT_NO_THROW({ UvtUtils::AppendUdpData(originalData, 1, EncodedData); });
  EncodedData.resize(EncodedData.size() - 1); // Remove one byte to simulate not enough data

  std::vector<uint8_t> DecodedData;

  EXPECT_NO_THROW({
    auto restData = UvtUtils::ExtractUdpData(EncodedData, DecodedData);
    // No data should be extracted yet
    EXPECT_EQ(DecodedData.size(), 0);
    EXPECT_EQ(EncodedData, restData);
  });
}

TEST(UvtUtilsTest, ExtractUdpDataMultipleCall) {

  std::vector<uint8_t> originalData = {0x01, 0x02, 0x03, 0x04};

  std::vector<uint8_t> EncodedData;
  EXPECT_NO_THROW({ UvtUtils::AppendUdpData(originalData, 1, EncodedData); });

  std::vector<uint8_t> DecodedData;
  std::vector<uint8_t> restData;
  EXPECT_NO_THROW({
    restData = UvtUtils::ExtractUdpData(EncodedData, DecodedData);
    EXPECT_EQ(DecodedData.size(), originalData.size());
    EXPECT_EQ(DecodedData, originalData);
    EXPECT_EQ(restData.size(), 0); // All data should be extracted
  });

  EXPECT_EQ(originalData.size(), 4);

  EncodedData.clear();

  EXPECT_NO_THROW({ UvtUtils::AppendUdpData(originalData, 2, EncodedData); });
  EXPECT_EQ(EncodedData.size(), 8 );
  EXPECT_NO_THROW({
    restData = UvtUtils::ExtractUdpData(EncodedData, DecodedData);
    EXPECT_EQ(DecodedData.size(), originalData.size());
    EXPECT_EQ(DecodedData, originalData);
    EXPECT_EQ(restData.size(), 0); // All data should be extracted
    EXPECT_EQ(restData, std::vector<uint8_t>{}); // All data should be extracted
  });
}
