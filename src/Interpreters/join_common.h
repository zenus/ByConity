#pragma once

#include <Core/Block.h>
#include <Interpreters/IJoin.h>
#include <Interpreters/ActionsDAG.h>
#include <Interpreters/ExpressionActions.h>

namespace DB
{

struct ColumnWithTypeAndName;
class TableJoin;
class IColumn;
using ColumnRawPtrs = std::vector<const IColumn *>;

namespace JoinCommon
{
bool isNullable(const DataTypePtr & type);
bool canBecomeNullable(const DataTypePtr & type);
DataTypePtr convertTypeToNullable(const DataTypePtr & type);
DataTypePtr tryConvertTypeToNullable(const DataTypePtr & type);
void convertColumnToNullable(ColumnWithTypeAndName & column, bool remove_low_card = false);
void convertColumnsToNullable(Block & block, size_t starting_pos = 0);
void convertColumnToNullable2(ColumnWithTypeAndName & column);

void removeColumnNullability(ColumnWithTypeAndName & column);
void changeColumnRepresentation(const ColumnPtr & src_column, ColumnPtr & dst_column);
ColumnPtr emptyNotNullableClone(const ColumnPtr & column);
Columns materializeColumns(const Block & block, const Names & names,
                           const std::vector<bool> *null_safeColumns);
ColumnRawPtrs materializeColumnsInplace(Block & block, const Names & names,
                                        const std::vector<bool> *null_safeColumns);
ColumnRawPtrs getRawPointers(const Columns & columns);
void removeLowCardinalityInplace(Block & block);
void removeLowCardinalityInplace(Block & block, const Names & names, bool change_type = true);
void restoreLowCardinalityInplace(Block & block);

ColumnRawPtrs extractKeysForJoin(Block & block_keys, const Names & key_names_right,
                                 const std::vector<bool> *null_safeColumns);

/// Throw an exception if blocks have different types of key columns. Compare up to Nullability.
void checkTypesOfKeys(const Block & block_left, const Names & key_names_left, const Block & block_right, const Names & key_names_right);
bool isJoinCompatibleTypes(const DataTypePtr & left, const DataTypePtr & right);

void createMissedColumns(Block & block);
void joinTotals(Block left_totals, Block right_totals, const TableJoin & table_join, Block & out_block);

void addDefaultValues(IColumn & column, const DataTypePtr & type, size_t count);

bool typesEqualUpToNullability(DataTypePtr left_type, DataTypePtr right_type);

void changeLowCardinalityInplace(ColumnWithTypeAndName & column);

}

/// Creates result from right table data in RIGHT and FULL JOIN when keys are not present in left table.
class NotJoined
{
public:
    NotJoined(const TableJoin & table_join, const Block & saved_block_sample_, const Block & right_sample_block,
              const Block & result_sample_block_);

    void correctLowcardAndNullability(MutableColumns & columns_right);
    void addLeftColumns(Block & block, size_t rows_added) const;
    void addRightColumns(Block & block, MutableColumns & columns_right) const;
    void copySameKeys(Block & block) const;

protected:
    Block saved_block_sample;
    Block result_sample_block;

    ~NotJoined() = default;

private:
    /// Indices of columns in result_sample_block that should be generated
    std::vector<size_t> column_indices_left;
    /// Indices of columns that come from the right-side table: right_pos -> result_pos
    std::unordered_map<size_t, size_t> column_indices_right;
    ///
    std::unordered_map<size_t, size_t> same_result_keys;
    /// Which right columns (saved in parent) need nullability change before placing them in result block
    std::vector<size_t> right_nullability_adds;
    std::vector<size_t> right_nullability_removes;
    /// Which right columns (saved in parent) need LowCardinality change before placing them in result block
    std::vector<std::pair<size_t, ColumnPtr>> right_lowcard_changes;

    void setRightIndex(size_t right_pos, size_t result_position);
    void extractColumnChanges(size_t right_pos, size_t result_pos);
};

}
