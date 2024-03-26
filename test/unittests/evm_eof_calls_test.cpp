// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "evm_fixture.hpp"
#include "evmone/eof.hpp"
#include "test/utils/bytecode.hpp"

using evmone::test::evm;
using namespace evmc::literals;

inline constexpr auto max_uint256 =
    0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff_bytes32;

// Controlled amount of gas crossing the minimum callee/retained gas thresholds.
inline constexpr auto safe_call_gas = 400000;

TEST_P(evm, extdelegatecall)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    constexpr auto callee = 0xca11ee_address;
    host.access_account(callee);
    host.accounts[callee].code = "EF00"_hex;

    auto code = eof_bytecode(mstore(0, push(1) + push0() + OP_SUB) +
                                 extdelegatecall(callee).input(0x2, 0x3) +
                                 returndatacopy(0x4, 0x0, 0x5) + ret(0, 8),
        4);

    auto call_output = bytes{0xa, 0xb, 0xc, 0xd, 0xe};
    host.call_result.output_data = call_output.data();
    host.call_result.output_size = call_output.size();
    host.call_result.gas_left = 1;

    msg.value.bytes[17] = 0xfe;

    execute(safe_call_gas, code);

    auto gas_before_call = 3 + 2 + 3 + 3 + 6 + 3 * 3 + 100;
    auto gas_left = safe_call_gas - gas_before_call;
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.gas, gas_left - gas_left / 64);
    EXPECT_EQ(call_msg.input_size, 3);
    EXPECT_EQ(call_msg.value.bytes[17], 0xfe);

    ASSERT_EQ(result.output_size, 8);
    EXPECT_EQ(output, (bytes{0xff, 0xff, 0xff, 0xff, 0xa, 0xb, 0xc, 0xd}));

    EXPECT_GAS_USED(EVMC_SUCCESS,
        gas_before_call + call_msg.gas - host.call_result.gas_left + 3 + 3 + 3 + 3 + 3 + 3 + 3);
}

TEST_P(evm, extdelegatecall_oog_depth_limit)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    constexpr auto callee = 0xca11ee_address;
    host.access_account(callee);
    host.accounts[callee].code = "EF00"_hex;

    msg.depth = 1024;
    const auto code = eof_bytecode(extdelegatecall(callee) + ret_top(), 3);

    execute(safe_call_gas, code);
    EXPECT_EQ(host.recorded_calls.size(), 0);
    auto expected_gas_used = 3 * 3 + 100 + 3 + 3 + 3 + 3 + 3;
    EXPECT_GAS_USED(EVMC_SUCCESS, expected_gas_used);
    EXPECT_OUTPUT_INT(1);

    execute(expected_gas_used, code);
    EXPECT_STATUS(EVMC_SUCCESS);  // MIN_CALLEE_GAS failure is light failure as well.
    EXPECT_OUTPUT_INT(1);
}

TEST_P(evm, extcall_failing_with_value)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    constexpr auto callee = 0xca11ee_address;
    host.access_account(callee);
    host.accounts[callee] = {};

    const auto code = eof_bytecode(extcall(callee).input(0x0, 0xff).value(0x1) + OP_STOP, 4);

    // Fails on balance check.
    execute(safe_call_gas, code);
    EXPECT_GAS_USED(EVMC_SUCCESS, 4 * 3 + 100 + 8 * 3 + 9000);
    EXPECT_EQ(host.recorded_calls.size(), 0);  // There was no call().

    // Fails on value transfer additional cost - minimum gas limit that triggers this
    execute(4 * 3 + 100 + 8 * 3, code);
    EXPECT_STATUS(EVMC_OUT_OF_GAS);
    EXPECT_EQ(host.recorded_calls.size(), 0);  // There was no call().

    // Fails on value transfer additional cost - maximum gas limit that triggers this
    execute(4 * 3 + 100 + 8 * 3 + 9000 - 1, code);
    EXPECT_STATUS(EVMC_OUT_OF_GAS);
    EXPECT_EQ(host.recorded_calls.size(), 0);  // There was no call().
}

TEST_P(evm, extcall_with_value_depth_limit)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;

    constexpr auto call_dst = 0x00000000000000000000000000000000000000aa_address;
    host.accounts[call_dst] = {};

    msg.depth = 1024;
    execute(eof_bytecode(extcall(call_dst).input(0x0, 0xff).value(0x1) + OP_STOP, 4));

    EXPECT_EQ(gas_used, 4 * 3 + 2600 + 8 * 3 + 9000);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(host.recorded_calls.size(), 0);
}

TEST_P(evm, extcall_depth_limit)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    constexpr auto callee = 0xca11ee_address;
    host.access_account(callee);
    host.accounts[callee].code = "EF00"_hex;
    msg.depth = 1024;

    for (auto op : {OP_EXTCALL, OP_EXTDELEGATECALL, OP_EXTSTATICCALL})
    {
        const auto code = eof_bytecode(push(callee) + 3 * push0() + op + ret_top(), 4);
        execute(code);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(host.recorded_calls.size(), 0);
        EXPECT_OUTPUT_INT(1);
    }
}

TEST_P(evm, extcall_value_zero_to_nonexistent_account)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    host.call_result.gas_left = 1000;

    const auto code = eof_bytecode(extcall(0xaa).input(0, 0x40) + OP_STOP, 4);

    execute(safe_call_gas, code);
    auto gas_before_call = 4 * 3 + 2 * 3 + 2600;
    auto gas_left = safe_call_gas - gas_before_call;
    ASSERT_EQ(host.recorded_calls.size(), 1);
    const auto& call_msg = host.recorded_calls.back();
    EXPECT_EQ(call_msg.kind, EVMC_CALL);
    EXPECT_EQ(call_msg.depth, 1);
    EXPECT_EQ(call_msg.gas, gas_left - gas_left / 64);
    EXPECT_EQ(call_msg.input_size, 64);
    EXPECT_EQ(call_msg.recipient, 0x00000000000000000000000000000000000000aa_address);
    EXPECT_EQ(call_msg.value.bytes[31], 0);

    EXPECT_GAS_USED(EVMC_SUCCESS, gas_before_call + call_msg.gas - host.call_result.gas_left);
}

TEST_P(evm, extcall_new_account_creation_cost)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;
    constexpr auto call_dst = 0x00000000000000000000000000000000000000ad_address;
    constexpr auto msg_dst = 0x0000000000000000000000000000000000000003_address;
    const auto code =
        eof_bytecode(extcall(call_dst).value(calldataload(0)).input(0, 0) + ret_top(), 4);

    msg.recipient = msg_dst;

    rev = EVMC_PRAGUE;
    {
        auto gas_before_call = 3 * 3 + 3 + 3 + 2600;
        auto gas_left = safe_call_gas - gas_before_call;

        host.accounts[msg.recipient].set_balance(0);
        execute(safe_call_gas, code, "00"_hex);
        EXPECT_OUTPUT_INT(0);
        ASSERT_EQ(host.recorded_calls.size(), 1);
        auto& call_msg = host.recorded_calls.back();
        EXPECT_EQ(call_msg.recipient, call_dst);
        EXPECT_EQ(call_msg.gas, gas_left - gas_left / 64);
        EXPECT_EQ(call_msg.sender, msg_dst);
        EXPECT_EQ(call_msg.value.bytes[31], 0);
        EXPECT_EQ(call_msg.input_size, 0);
        EXPECT_GAS_USED(EVMC_SUCCESS, gas_before_call + call_msg.gas + 3 + 3 + 3 + 3 + 3);
        ASSERT_EQ(host.recorded_account_accesses.size(), 4);
        EXPECT_EQ(host.recorded_account_accesses[0], 0x00_address);   // EIP-2929 tweak
        EXPECT_EQ(host.recorded_account_accesses[1], msg.recipient);  // EIP-2929 tweak
        EXPECT_EQ(host.recorded_account_accesses[2], call_dst);       // ?
        EXPECT_EQ(host.recorded_account_accesses[3], call_dst);       // Call.
        host.recorded_account_accesses.clear();
        host.recorded_calls.clear();
    }
    {
        auto gas_before_call = 3 * 3 + 3 + 3 + 2600 + 25000 + 9000;
        auto gas_left = safe_call_gas - gas_before_call;

        host.accounts[msg.recipient].set_balance(1);
        execute(safe_call_gas, code,
            "0000000000000000000000000000000000000000000000000000000000000001"_hex);
        EXPECT_OUTPUT_INT(0);
        ASSERT_EQ(host.recorded_calls.size(), 1);
        auto& call_msg = host.recorded_calls.back();
        EXPECT_EQ(call_msg.recipient, call_dst);
        EXPECT_EQ(call_msg.gas, gas_left - gas_left / 64);
        EXPECT_EQ(call_msg.sender, msg_dst);
        EXPECT_EQ(call_msg.value.bytes[31], 1);
        EXPECT_EQ(call_msg.input_size, 0);
        EXPECT_GAS_USED(EVMC_SUCCESS, gas_before_call + call_msg.gas + 3 + 3 + 3 + 3 + 3);
        ASSERT_EQ(host.recorded_account_accesses.size(), 6);
        EXPECT_EQ(host.recorded_account_accesses[0], 0x00_address);   // EIP-2929 tweak
        EXPECT_EQ(host.recorded_account_accesses[1], msg.recipient);  // EIP-2929 tweak
        EXPECT_EQ(host.recorded_account_accesses[2], call_dst);       // ?
        EXPECT_EQ(host.recorded_account_accesses[3], call_dst);       // Account exist?.
        EXPECT_EQ(host.recorded_account_accesses[4], msg.recipient);  // Balance.
        EXPECT_EQ(host.recorded_account_accesses[5], call_dst);       // Call.
        host.recorded_account_accesses.clear();
        host.recorded_calls.clear();
    }
}

TEST_P(evm, extcall_oog_after_balance_check)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    // Create the call destination account.
    host.accounts[0x0000000000000000000000000000000000000000_address] = {};
    auto code = eof_bytecode(extcall(0).value(1) + OP_POP + OP_STOP, 4);
    execute(9112, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_P(evm, extcall_oog_after_depth_check)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    // Create the call recipient account.
    host.accounts[0x0000000000000000000000000000000000000000_address] = {};
    msg.depth = 1024;

    auto code = eof_bytecode(extcall(0).value(1) + OP_POP + OP_STOP, 4);
    execute(9112, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_P(evm, returndataload)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    const auto call_output =
        0x497f3c9f61479c1cfa53f0373d39d2bf4e5f73f71411da62f1d6b85c03a60735_bytes32;
    host.call_result.output_data = std::data(call_output.bytes);
    host.call_result.output_size = std::size(call_output.bytes);

    const auto code = eof_bytecode(extstaticcall(0) + returndataload(0) + ret_top(), 3);

    execute(code);
    EXPECT_EQ(bytes_view(result.output_data, result.output_size), call_output);
}

TEST_P(evm, returndataload_cost)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    const uint8_t call_output[32]{};
    host.call_result.output_data = std::data(call_output);
    host.call_result.output_size = std::size(call_output);
    host.call_result.gas_left = 0;

    execute(eof_bytecode(extstaticcall(0) + returndataload(0) + OP_STOP, 3));
    const auto gas_with = gas_used;
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    execute(eof_bytecode(extstaticcall(0) + push(0) + OP_STOP, 3));
    EXPECT_GAS_USED(EVMC_SUCCESS, gas_with - 3);
}

TEST_P(evm, returndataload_oog)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    const uint8_t call_output[32]{};
    host.call_result.output_data = std::data(call_output);
    host.call_result.output_size = std::size(call_output);
    host.call_result.gas_left = 0;

    constexpr auto retained_gas = 5000;
    constexpr auto gas = 3 * 3 + 100 + retained_gas * 64;
    // Uses OP_JUMPDEST to burn gas retained by the caller.
    execute(gas, eof_bytecode(extstaticcall(0) + (retained_gas - 3 - 3) * OP_JUMPDEST +
                                  returndataload(0) + OP_STOP,
                     3));
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);

    execute(gas, eof_bytecode(extstaticcall(0) + (retained_gas - 3 - 2) * OP_JUMPDEST +
                                  returndataload(0) + OP_STOP,
                     3));
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_P(evm, returndataload_outofrange)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    {
        const uint8_t call_output[31]{};
        host.call_result.output_data = std::data(call_output);
        host.call_result.output_size = std::size(call_output);

        execute(eof_bytecode(extstaticcall(0) + returndataload(0) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);
    }
    {
        const uint8_t call_output[32]{};
        host.call_result.output_data = std::data(call_output);
        host.call_result.output_size = std::size(call_output);

        execute(eof_bytecode(extstaticcall(0) + returndataload(1) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(31) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(32) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(max_uint256) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(0) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    }
    {
        const uint8_t call_output[34]{};
        host.call_result.output_data = std::data(call_output);
        host.call_result.output_size = std::size(call_output);

        execute(eof_bytecode(extstaticcall(0) + returndataload(3) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(max_uint256) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(1) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(2) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    }
    {
        const uint8_t call_output[64]{};
        host.call_result.output_data = std::data(call_output);
        host.call_result.output_size = std::size(call_output);

        execute(eof_bytecode(extstaticcall(0) + returndataload(33) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(max_uint256) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);


        execute(eof_bytecode(extstaticcall(0) + returndataload(1) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(31) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);

        execute(eof_bytecode(extstaticcall(0) + returndataload(32) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        execute(eof_bytecode(extstaticcall(0) + returndataload(0) + OP_STOP, 3));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    }
}

TEST_P(evm, returndataload_empty)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    execute(eof_bytecode(extstaticcall(0) + returndataload(0) + OP_STOP, 3));
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

    execute(eof_bytecode(extstaticcall(0) + returndataload(1) + OP_STOP, 3));
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);

    execute(eof_bytecode(extstaticcall(0) + returndataload(max_uint256) + OP_STOP, 3));
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);
}

TEST_P(evm, returndataload_outofrange_highbits)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    const uint8_t call_output[34]{};
    host.call_result.output_data = std::data(call_output);
    host.call_result.output_size = std::size(call_output);

    // Covers an incorrect cast of RETURNDATALOAD arg to `size_t` ignoring the high bits.
    const auto highbits =
        0x1000000000000000000000000000000000000000000000000000000000000000_bytes32;
    execute(eof_bytecode(extstaticcall(0) + returndataload(highbits) + OP_STOP, 3));
    EXPECT_EQ(result.status_code, EVMC_INVALID_MEMORY_ACCESS);
}

TEST_P(evm, extcall_gas_refund_aggregation_different_calls)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    constexpr auto callee = 0xca11ee_address;
    host.access_account(callee);
    host.accounts[callee].code = "EF00"_hex;
    host.accounts[msg.recipient].set_balance(1);
    host.call_result.status_code = EVMC_SUCCESS;
    host.call_result.gas_refund = 1;

    const auto code = eof_bytecode(
        extcall(callee) + extdelegatecall(callee) + extstaticcall(callee) + OP_STOP, 5);
    execute(code);
    EXPECT_STATUS(EVMC_SUCCESS);
    EXPECT_EQ(result.gas_refund, 3);
}

TEST_P(evm, extcall_gas_refund_aggregation_same_calls)
{
    // Not implemented in Advanced.
    if (is_advanced())
        return;

    rev = EVMC_PRAGUE;
    constexpr auto callee = 0xaa_address;
    host.access_account(callee);
    host.accounts[callee].code = "EF00"_hex;
    host.accounts[msg.recipient].set_balance(2);
    host.call_result.status_code = EVMC_SUCCESS;
    host.call_result.gas_refund = 1;

    execute(eof_bytecode(2 * extcall(callee).value(1).input(1, 1) + OP_STOP, 5));
    EXPECT_STATUS(EVMC_SUCCESS);
    EXPECT_EQ(result.gas_refund, 2);

    execute(eof_bytecode(2 * extdelegatecall(callee).input(1, 1) + OP_STOP, 4));
    EXPECT_STATUS(EVMC_SUCCESS);
    EXPECT_EQ(result.gas_refund, 2);

    execute(eof_bytecode(2 * extstaticcall(callee).input(1, 1) + OP_STOP, 4));
    EXPECT_STATUS(EVMC_SUCCESS);
    EXPECT_EQ(result.gas_refund, 2);
}
