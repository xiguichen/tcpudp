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

// Test for UvtUtils::AppendUdpData
TEST(UvtUtilsTest, AppendUdpData) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04};
    uint8_t id = 1;
    std::vector<uint8_t> outputBuffer;

    UvtUtils::AppendUdpData(data, id, outputBuffer);

    // Check the size of the output buffer
    EXPECT_EQ(outputBuffer.size(), HEADER_SIZE + data.size());

    // Check the header
    UvtHeader *header = reinterpret_cast<UvtHeader *>(outputBuffer.data());
    EXPECT_EQ(header->size, static_cast<uint16_t>(data.size()));
    EXPECT_EQ(header->id, id);
    EXPECT_EQ(header->checksum, UvtUtils::calculateChecksum(data.data(), data.size()));
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
    EXPECT_EQ(header->checksum, UvtUtils::calculateChecksum(data.data(), data.size()));
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
    EXPECT_EQ(header->size, static_cast<uint16_t>(data.size()));
    EXPECT_EQ(header->id, id);
    EXPECT_EQ(header->checksum, UvtUtils::calculateChecksum(data.data(), data.size()));
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
    EXPECT_EQ(outputBuffer.size(), HEADER_SIZE + data1.size() + HEADER_SIZE + data2.size());

    // Check the first header
    UvtHeader *header1 = reinterpret_cast<UvtHeader *>(outputBuffer.data());
    EXPECT_EQ(header1->size, static_cast<uint16_t>(data1.size()));
    EXPECT_EQ(header1->id, id);
    EXPECT_EQ(header1->checksum, UvtUtils::calculateChecksum(data1.data(), data1.size()));

    // Check the second header
    UvtHeader *header2 = reinterpret_cast<UvtHeader *>(outputBuffer.data() + HEADER_SIZE + data1.size());
    EXPECT_EQ(header2->size, static_cast<uint16_t>(data2.size()));
    EXPECT_EQ(header2->id, id);
    EXPECT_EQ(header2->checksum, UvtUtils::calculateChecksum(data2.data(), data2.size()));
}



