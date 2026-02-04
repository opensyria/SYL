// Copyright (c) 2024-present The OpenSY developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef OPENSY_UTIL_LRU_CACHE_H
#define OPENSY_UTIL_LRU_CACHE_H

#include <cassert>
#include <cstddef>
#include <list>
#include <optional>
#include <unordered_map>

/**
 * A thread-unsafe LRU (Least Recently Used) cache.
 * 
 * This cache provides O(1) lookup, insertion, and eviction operations.
 * The caller is responsible for synchronization if used from multiple threads.
 * 
 * Implementation uses:
 * - std::unordered_map for O(1) key lookup
 * - std::list for O(1) LRU ordering (most recent at front, least recent at back)
 * 
 * @tparam Key   The key type (must be hashable)
 * @tparam Value The value type
 * @tparam Hash  Hash function for Key (defaults to std::hash<Key>)
 */
template<typename Key, typename Value, typename Hash = std::hash<Key>>
class LRUCache {
public:
    using key_type = Key;
    using value_type = Value;
    using size_type = std::size_t;

private:
    // Entry in the LRU list: stores key-value pair
    using ListEntry = std::pair<Key, Value>;
    using List = std::list<ListEntry>;
    using ListIterator = typename List::iterator;
    
    // Map from key to iterator in the list
    using Map = std::unordered_map<Key, ListIterator, Hash>;

    List m_list;              // Front = most recently used, Back = least recently used
    Map m_map;                // Key -> iterator into m_list
    size_type m_max_size;     // Maximum cache capacity

public:
    /**
     * Construct an LRU cache with the given maximum size.
     * 
     * @param max_size Maximum number of entries (must be > 0)
     */
    explicit LRUCache(size_type max_size) : m_max_size(max_size)
    {
        assert(max_size > 0);
        m_map.reserve(max_size);
    }

    // Non-copyable, movable
    LRUCache(const LRUCache&) = delete;
    LRUCache& operator=(const LRUCache&) = delete;
    LRUCache(LRUCache&&) = default;
    LRUCache& operator=(LRUCache&&) = default;

    /**
     * Get the maximum capacity of the cache.
     */
    size_type max_size() const noexcept { return m_max_size; }

    /**
     * Get the current number of entries in the cache.
     */
    size_type size() const noexcept { return m_map.size(); }

    /**
     * Check if the cache is empty.
     */
    bool empty() const noexcept { return m_map.empty(); }

    /**
     * Check if the cache is at capacity.
     */
    bool full() const noexcept { return m_map.size() >= m_max_size; }

    /**
     * Check if a key exists in the cache.
     * Does NOT update LRU order.
     */
    bool contains(const Key& key) const
    {
        return m_map.find(key) != m_map.end();
    }

    /**
     * Get a value from the cache.
     * Updates LRU order (marks as recently used).
     * 
     * @param key The key to look up
     * @return The value if found, std::nullopt otherwise
     */
    std::optional<Value> get(const Key& key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end()) {
            return std::nullopt;
        }

        // Move to front (most recently used)
        m_list.splice(m_list.begin(), m_list, it->second);
        return it->second->second;
    }

    /**
     * Get a const reference to a value from the cache.
     * Updates LRU order (marks as recently used).
     * 
     * @param key The key to look up
     * @return Pointer to value if found, nullptr otherwise
     */
    const Value* get_ptr(const Key& key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end()) {
            return nullptr;
        }

        // Move to front (most recently used)
        m_list.splice(m_list.begin(), m_list, it->second);
        return &(it->second->second);
    }

    /**
     * Peek at a value without updating LRU order.
     * 
     * @param key The key to look up
     * @return The value if found, std::nullopt otherwise
     */
    std::optional<Value> peek(const Key& key) const
    {
        auto it = m_map.find(key);
        if (it == m_map.end()) {
            return std::nullopt;
        }
        return it->second->second;
    }

    /**
     * Insert or update a value in the cache.
     * If the cache is full, evicts the least recently used entry.
     * 
     * @param key   The key
     * @param value The value to store
     * @return true if an existing entry was updated, false if new entry was inserted
     */
    bool insert(const Key& key, const Value& value)
    {
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            // Update existing entry
            it->second->second = value;
            // Move to front
            m_list.splice(m_list.begin(), m_list, it->second);
            return true;
        }

        // New entry - check if we need to evict
        if (m_map.size() >= m_max_size) {
            evict_lru();
        }

        // Insert at front
        m_list.emplace_front(key, value);
        m_map[key] = m_list.begin();
        return false;
    }

    /**
     * Insert or update a value using move semantics.
     * 
     * @param key   The key
     * @param value The value to store (moved)
     * @return true if an existing entry was updated, false if new entry was inserted
     */
    bool insert(const Key& key, Value&& value)
    {
        auto it = m_map.find(key);
        if (it != m_map.end()) {
            // Update existing entry
            it->second->second = std::move(value);
            // Move to front
            m_list.splice(m_list.begin(), m_list, it->second);
            return true;
        }

        // New entry - check if we need to evict
        if (m_map.size() >= m_max_size) {
            evict_lru();
        }

        // Insert at front
        m_list.emplace_front(key, std::move(value));
        m_map[key] = m_list.begin();
        return false;
    }

    /**
     * Remove a specific key from the cache.
     * 
     * @param key The key to remove
     * @return true if the key was found and removed, false otherwise
     */
    bool erase(const Key& key)
    {
        auto it = m_map.find(key);
        if (it == m_map.end()) {
            return false;
        }

        m_list.erase(it->second);
        m_map.erase(it);
        return true;
    }

    /**
     * Clear all entries from the cache.
     */
    void clear()
    {
        m_list.clear();
        m_map.clear();
    }

    /**
     * Resize the cache. If new size is smaller, evicts LRU entries.
     * 
     * @param new_max_size New maximum size (must be > 0)
     */
    void resize(size_type new_max_size)
    {
        assert(new_max_size > 0);
        m_max_size = new_max_size;
        
        // Evict until we're within the new limit
        while (m_map.size() > m_max_size) {
            evict_lru();
        }
    }

    /**
     * Get cache statistics for monitoring.
     */
    struct Stats {
        size_type size;
        size_type max_size;
        double load_factor;  // size / max_size
    };

    Stats stats() const
    {
        Stats s;
        s.size = m_map.size();
        s.max_size = m_max_size;
        s.load_factor = m_max_size > 0 ? static_cast<double>(s.size) / m_max_size : 0.0;
        return s;
    }

private:
    /**
     * Evict the least recently used entry (at back of list).
     */
    void evict_lru()
    {
        if (m_list.empty()) return;

        // Remove from map
        m_map.erase(m_list.back().first);
        // Remove from list
        m_list.pop_back();
    }
};

#endif // OPENSY_UTIL_LRU_CACHE_H
