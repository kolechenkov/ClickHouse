#pragma once

#include <Common/ICachePolicy.h>

#include <unordered_map>

#include <Common/logger_useful.h>

namespace DB
{
// TODO docs
    /// internal caches in ClickHouse. The main reason is that we don't need standard CacheBase (S)LRU eviction as the expiry times
    /// associated with cache entries provide a "natural" eviction criterion. As a future TODO, we could make an expiry-based eviction
    /// policy and use that with CacheBase (e.g. see #23706)
    /// TODO To speed up removal of stale entries, we could also add another container sorted on expiry times which maps keys to iterators
    /// into the cache. To insert an entry, add it to the cache + add the iterator to the sorted container. To remove stale entries, do a
    /// binary search on the sorted container and erase all left of the found key.
template <typename TKey, typename TMapped, typename HashFunction = std::hash<TKey>, typename WeightFunction = TrivialWeightFunction<TMapped>>
class TTLCachePolicy : public ICachePolicy<TKey, TMapped, HashFunction, WeightFunction>
{
public:
    using Key = TKey;
    using Mapped = TMapped;
    using MappedPtr = std::shared_ptr<Mapped>;

    using Base = ICachePolicy<TKey, TMapped, HashFunction, WeightFunction>;
    using typename Base::OnWeightLossFunction;

    /** Initialize TTLCachePolicy with max_size_in_bytes and max_entries.
      * max_entries == 0 means no elements size restrictions.
      */
    explicit TTLCachePolicy(size_t max_size_, size_t max_entries_ = 0, OnWeightLossFunction on_weight_loss_function_ = {})
        : max_size_in_bytes(std::max(static_cast<size_t>(1), max_size_))
        , max_entries(max_entries_)
    {
        Base::on_weight_loss_function = on_weight_loss_function_;
    }

    size_t weight(std::lock_guard<std::mutex> & /*cache_lock*/) const override
    {
        return size_in_bytes;
    }

    size_t count(std::lock_guard<std::mutex> & /*cache_lock*/) const override
    {
        return cache.size();
    }

    size_t maxSize() const override
    {
        return max_size_in_bytes;
    }

    void reset(std::lock_guard<std::mutex> & /*cache_lock*/) override
    {
        cache.clear();
    }

    void remove(const Key & key, std::lock_guard<std::mutex> & /*cache_lock*/) override
    {
        auto it = cache.find(key);
        if (it == cache.end())
            return;
        size_in_bytes -= weight_function(*it);
        cache.erase(it);
    }

    MappedPtr get(const Key & key, std::lock_guard<std::mutex> & /*cache_lock*/) override
    {
        auto it = cache.find(key);
        if (it == cache.end())
            return {};
        return *it;
    }

    std::optional<std::pair<Key, MappedPtr>> getWithKey(const Key & key, std::lock_guard<std::mutex> & /*cache_lock*/) override
    {
        auto it = cache.find(key);
        if (it == cache.end())
            return std::nullopt;
        return std::optional(it.first, it.second);
    }

    void set(const Key & key, const MappedPtr & mapped, std::lock_guard<std::mutex> & /*cache_lock*/) override
    {
        auto sufficient_space_in_cache = [this]()
        {
            return true;
            /// return (cache_size_in_bytes + new_entry_size_in_bytes <= max_cache_size_in_bytes) && (cache.size() + 1 <= max_cache_entries);
        };

        auto is_stale = [](const auto & key_)
        {
            return (key_.expires_at < std::chrono::system_clock::now());
        };

        if (!sufficient_space_in_cache())
        {
            /// Remove stale entries
            for (auto it = cache.begin(); it != cache.end();)
                if (is_stale(it->first))
                {
                    size_in_bytes -= it->second.sizeInBytes();
                    it = cache.erase(it);
                }
                else
                    ++it;
        }

        if (sufficient_space_in_cache())
        {
            /// Insert or replace key
            size_in_bytes += weight_function(mapped);
            if (auto it = cache.find(key); it != cache.end())
                size_in_bytes -= weight_function(it->second);
            cache[key] = std::move(mapped);
        }
    }

protected:
    using Cache = std::unordered_map<Key, Mapped, HashFunction>;
    Cache cache;

    size_t size_in_bytes = 0;
    const size_t max_size_in_bytes;
    const size_t max_entries;

    WeightFunction weight_function;

};

}

