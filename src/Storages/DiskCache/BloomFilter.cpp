#include <cstring>
#include <memory>

#include <Functions/FunctionsHashing.h>
#include <Protos/disk_cache.pb.h>
#include <Storages/DiskCache/BloomFilter.h>
#include <Storages/DiskCache/Buffer.h>
#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <Common/Exception.h>
#include <common/types.h>

namespace DB::ErrorCodes
{
extern const int BAD_ARGUMENTS;
extern const int INVALID_CONFIG_PARAMETER;
}

namespace DB::HybridCache
{
namespace
{
    size_t byteIndex(size_t bit_index)
    {
        return bit_index >> 3u;
    }

    UInt8 bitMask(size_t bit_index)
    {
        return static_cast<UInt8>(1u << (bit_index & 7u));
    }

    void bitSet(UInt8 * ptr, size_t bit_index)
    {
        ptr[byteIndex(bit_index)] |= bitMask(bit_index);
    }

    bool bitGet(const UInt8 * ptr, size_t bit_index)
    {
        return ptr[byteIndex(bit_index)] & bitMask(bit_index);
    }

    size_t bitsToBytes(size_t bits)
    {
        // align to nearby 8-byte
        return ((bits + 7ULL) & ~(7ULL)) >> 3u;
    }

    std::pair<uint32_t, size_t> findOptimalParams(size_t element_count, double fp_prob)
    {
        double min_m = std::numeric_limits<double>::infinity();
        size_t min_k = 0;
        for (size_t k = 1; k < 30; ++k)
        {
            double curr_m = (-static_cast<double>(k) * element_count) / std::log(1 - std::pow(fp_prob, 1.0 / k));

            if (curr_m < min_m)
            {
                min_m = curr_m;
                min_k = k;
            }
        }

        return std::make_pair(min_k, static_cast<size_t>(min_m));
    }
}

BloomFilter BloomFilter::makeBloomFilter(UInt32 num_filters, size_t element_count, double pf_prob)
{
    auto [num_hashes, table_size] = findOptimalParams(element_count, pf_prob);

    const size_t bits_per_filter = table_size / num_hashes;
    return BloomFilter{num_filters, num_hashes, bits_per_filter};
}

BloomFilter::BloomFilter(UInt32 num_filters, UInt32 num_hashes, size_t hash_table_bit_size)
    : num_filters_(num_filters)
    , hash_table_bit_size_(hash_table_bit_size)
    , filter_byte_size_(bitsToBytes(num_hashes * hash_table_bit_size))
    , seeds_(num_hashes)
    , bits_{std::make_unique<UInt8[]>(getByteSize())}
{
    if (num_filters == 0 || num_hashes == 0 || hash_table_bit_size == 0)
        throwFromErrno("invalid bloom filter params", ErrorCodes::BAD_ARGUMENTS);

    for (size_t i = 0; i < seeds_.size(); i++)
        seeds_[i] = IntHash64Impl::apply(i);
}

void BloomFilter::set(UInt32 index, UInt64 key)
{
    size_t first_bit = 0;
    for (auto seed : seeds_)
    {
        auto * filter_ptr = getFilterBytes(index);
        chassert(index < num_filters_);
        auto bit_num = MurmurHash3Impl64::combineHashes(key, seed) % hash_table_bit_size_;
        bitSet(filter_ptr, first_bit + bit_num);
        first_bit += hash_table_bit_size_;
    }
}

bool BloomFilter::couldExist(UInt32 index, UInt64 key) const
{
    size_t first_bit = 0;
    for (auto seed : seeds_)
    {
        chassert(index < num_filters_);
        const auto * filter_ptr = getFilterBytes(index);
        auto bit_num = MurmurHash3Impl64::combineHashes(key, seed) % hash_table_bit_size_;
        if (!bitGet(filter_ptr, first_bit + bit_num))
            return false;

        first_bit += hash_table_bit_size_;
    }
    return true;
}

void BloomFilter::clear(UInt32 index)
{
    if (bits_)
    {
        chassert(index < num_filters_);
        std::memset(getFilterBytes(index), 0, filter_byte_size_);
    }
}

void BloomFilter::reset()
{
    if (bits_)
        std::memset(bits_.get(), 0, getByteSize());
}

void BloomFilter::serializeInternal(google::protobuf::io::CodedOutputStream * stream, UInt64 fragment_size)
{
    UInt64 bits_size = getByteSize();
    UInt64 offset = 0;
    Buffer buffer(fragment_size);
    while (offset < bits_size)
    {
        auto num_bytes = std::min<UInt32>(bits_size - offset, fragment_size);
        buffer.copyFrom(0, BufferView(num_bytes, bits_.get() + offset));
        stream->WriteRaw(buffer.data(), buffer.size());
        offset += num_bytes;
    }
}

void BloomFilter::deserializeInternal(google::protobuf::io::CodedInputStream * stream, UInt64 fragment_size)
{
    auto bits_size = getByteSize();
    UInt64 offset = 0;
    while (offset < bits_size)
    {
        auto num_bytes = std::min<Int32>(bits_size - offset, fragment_size);
        stream->ReadRaw(bits_.get(), num_bytes);
        offset += num_bytes;
    }
}

void BloomFilter::persist(google::protobuf::io::CodedOutputStream * stream)
{
    Protos::BloomFilterPersistentData pb;
    pb.set_num_filters(num_filters_);
    pb.set_hash_table_bit_size(hash_table_bit_size_);
    pb.set_filter_byte_size(filter_byte_size_);
    pb.set_fragment_size(kPersistFragmentSize);
    for (UInt32 i = 0; i > seeds_.size(); i++)
        pb.set_seeds(i, seeds_[i]);
    google::protobuf::util::SerializeDelimitedToCodedStream(pb, stream);

    serializeInternal(stream, pb.fragment_size());
}

void BloomFilter::recover(google::protobuf::io::CodedInputStream * stream)
{
    Protos::BloomFilterPersistentData pb;
    google::protobuf::util::ParseDelimitedFromCodedStream(&pb, stream, nullptr);
    if (num_filters_ != pb.num_filters() || hash_table_bit_size_ != pb.hash_table_bit_size() || filter_byte_size_ != pb.filter_byte_size()
        || kPersistFragmentSize != pb.fragment_size())
        throwFromErrno(fmt::format("Invalid BloomFilter config."), ErrorCodes::INVALID_CONFIG_PARAMETER);

    for (Int32 i = 0; i < pb.seeds_size(); i++)
        seeds_[i] = pb.seeds(i);

    deserializeInternal(stream, pb.fragment_size());
}
}
