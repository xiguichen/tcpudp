// #include <gtest/gtest.h>
// #include "TcpVirtualChannel.h"
//
// // Test fixture for TcpVirtualChannel
// class TcpVirtualChannelTest : public ::testing::Test {
// protected:
//     TcpVirtualChannel channel;
// };
//
// // Test open and close methods
// TEST_F(TcpVirtualChannelTest, OpenCloseTest) {
//     channel.open();
//     EXPECT_TRUE(channel.isOpen());
//     channel.close();
//     EXPECT_FALSE(channel.isOpen());
// }
//
// // Test send method
// TEST_F(TcpVirtualChannelTest, SendTest) {
//     const char *data = "test data";
//     size_t size = strlen(data);
//
//     channel.open();
//     ASSERT_TRUE(channel.isOpen());
//
//     // Assuming send returns true on success
//     EXPECT_TRUE(channel.send(data, size));
//
//     channel.close();
// }
//
// // Test receive callback
// TEST_F(TcpVirtualChannelTest, ReceiveCallbackTest) {
//     bool callbackCalled = false;
//     auto callback = [&callbackCalled](const char *data, size_t size) {
//         callbackCalled = true;
//         EXPECT_STREQ(data, "callback data");
//         EXPECT_EQ(size, strlen("callback data"));
//     };
//
//     channel.setReceiveCallback(callback);
//
//     // Simulate receiving data
//     channel.receiveCallback("callback data", strlen("callback data"));
//     EXPECT_TRUE(callbackCalled);
// }
