#include "catalog.h"
#include <iostream>
#include <cstdlib>

using namespace std;

void Catalog::addTable(const TableStats& t) {
    tables_[t.name] = t;
}

const TableStats* Catalog::getTable(const string& name) const {
    auto it = tables_.find(name);
    return (it != tables_.end()) ? &it->second : nullptr;
}

const ColumnStats* Catalog::getColumn(const string& table, const string& col) const {
    auto t = getTable(table);
    if (!t) return nullptr;
    return t->getColumn(col);
}

const IndexInfo* Catalog::findIndex(const string& table, const string& col) const {
    auto t = getTable(table);
    if (!t) return nullptr;
    for (const auto& idx : t->indices) {
        if (!idx.columns.empty() && idx.columns[0] == col) {
            return &idx;
        }
    }
    return nullptr;
}

vector<const IndexInfo*> Catalog::findAllIndices(const string& table, const string& col) const {
    vector<const IndexInfo*> result;
    auto t = getTable(table);
    if (!t) return result;
    for (const auto& idx : t->indices) {
        if (!idx.columns.empty() && idx.columns[0] == col) {
            result.push_back(&idx);
        }
    }
    return result;
}

string Catalog::toJSON() const {
    ostringstream ss;
    ss << "{\"tables\":[";
    bool firstTable = true;
    for (const auto& pair : tables_) {
        if (!firstTable) ss << ",";
        firstTable = false;
        const auto& t = pair.second;
        ss << "{\"name\":\"" << t.name << "\""
           << ",\"numTuples\":" << t.numTuples
           << ",\"tupleSize\":" << t.tupleSize
           << ",\"numBlocks\":" << t.numBlocks
           << ",\"blockingFactor\":" << t.blockingFactor
           << ",\"columns\":[";
        bool firstCol = true;
        for (const auto& c : t.columns) {
            if (!firstCol) ss << ",";
            firstCol = false;
            ss << "{\"name\":\"" << c.name << "\""
               << ",\"dataType\":\"" << c.dataType << "\""
               << ",\"numDistinct\":" << c.numDistinct
               << ",\"minVal\":" << c.minVal
               << ",\"maxVal\":" << c.maxVal
               << ",\"isPrimaryKey\":" << (c.isPrimaryKey ? "true" : "false")
               << ",\"isForeignKey\":" << (c.isForeignKey ? "true" : "false");
            if (c.isForeignKey) {
                ss << ",\"fkRefTable\":\"" << c.fkRefTable << "\""
                   << ",\"fkRefColumn\":\"" << c.fkRefColumn << "\"";
            }
            ss << "}";
        }
        ss << "],\"indices\":[";
        bool firstIdx = true;
        for (const auto& idx : t.indices) {
            if (!firstIdx) ss << ",";
            firstIdx = false;
            ss << "{\"name\":\"" << idx.name << "\""
               << ",\"type\":\"" << idx.typeString() << "\""
               << ",\"columns\":[";
            for (size_t i = 0; i < idx.columns.size(); i++) {
                if (i > 0) ss << ",";
                ss << "\"" << idx.columns[i] << "\"";
            }
            ss << "],\"isClustered\":" << (idx.isClustered ? "true" : "false")
               << ",\"isUnique\":" << (idx.isUnique ? "true" : "false")
               << ",\"fanout\":" << idx.fanout
               << ",\"height\":" << idx.height;
            if (idx.type == IndexType::HASH) {
                ss << ",\"numBuckets\":" << idx.numBuckets
                   << ",\"avgOverflowChainLen\":" << idx.avgOverflowChainLen;
            }
            ss << "}";
        }
        ss << "]}";
    }
    ss << "]}";
    return ss.str();
}

void Catalog::initUniversityDB() {
    // instructor(ID, name, dept_name, salary)
    {
        TableStats t;
        t.name = "instructor";
        t.numTuples = 5000;
        t.tupleSize = 80;
        t.columns = {
            {"ID",        "int",    5000, 1, 5000, true,  false, "", ""},
            {"name",      "string", 4500, 0, 0,    false, false, "", ""},
            {"dept_name", "string", 20,   0, 0,    false, true,  "department", "dept_name"},
            {"salary",    "float",  500,  30000, 120000, false, false, "", ""}
        };
        t.indices = {
            {"idx_instructor_id",        IndexType::BTREE, "instructor", {"ID"},        true,  true,  100, 0, 0, 0},
            {"idx_instructor_dept",      IndexType::BTREE, "instructor", {"dept_name"}, false, false, 100, 0, 0, 0},
            {"idx_instructor_salary",    IndexType::BTREE, "instructor", {"salary"},    false, false, 100, 0, 0, 0}
        };
        t.computeDerived();
        addTable(t);
    }

    //student(ID, name, dept_name, tot_cred)
    {
        TableStats t;
        t.name = "student";
        t.numTuples = 10000;
        t.tupleSize = 80;
        t.columns = {
            {"ID",        "int",    10000, 1, 10000, true,  false, "", ""},
            {"name",      "string", 9000,  0, 0,     false, false, "", ""},
            {"dept_name", "string", 20,    0, 0,     false, true,  "department", "dept_name"},
            {"tot_cred",  "int",    200,   0, 150,   false, false, "", ""}
        };
        t.indices = {
            {"idx_student_id",   IndexType::BTREE, "student", {"ID"},        true,  true,  100, 0, 0, 0},
            {"idx_student_dept", IndexType::BTREE, "student", {"dept_name"}, false, false, 100, 0, 0, 0}
        };
        t.computeDerived();
        addTable(t);
    }

    //takes(ID, course_id, sec_id, semester, year, grade)
    {
        TableStats t;
        t.name = "takes";
        t.numTuples = 30000;
        t.tupleSize = 60;
        t.columns = {
            {"ID",        "int",    2500, 1, 10000, false, true, "student", "ID"},
            {"course_id", "string", 500,  0, 0,     false, true, "course", "course_id"},
            {"sec_id",    "string", 10,   0, 0,     false, false, "", ""},
            {"semester",  "string", 3,    0, 0,     false, false, "", ""},
            {"year",      "int",    10,   2010, 2025, false, false, "", ""},
            {"grade",     "string", 14,   0, 0,     false, false, "", ""}
        };
        t.indices = {
            {"idx_takes_id",     IndexType::BTREE, "takes", {"ID"},        false, false, 100, 0, 0, 0},
            {"idx_takes_course", IndexType::BTREE, "takes", {"course_id"}, false, false, 100, 0, 0, 0}
        };
        t.computeDerived();
        addTable(t);
    }

    //course(course_id, title, dept_name, credits)
    {
        TableStats t;
        t.name = "course";
        t.numTuples = 500;
        t.tupleSize = 100;
        t.columns = {
            {"course_id", "string", 500,  0, 0,  true,  false, "", ""},
            {"title",     "string", 480,  0, 0,  false, false, "", ""},
            {"dept_name", "string", 20,   0, 0,  false, true,  "department", "dept_name"},
            {"credits",   "int",    6,    1, 6,  false, false, "", ""}
        };
        t.indices = {
            {"idx_course_id",   IndexType::BTREE, "course", {"course_id"}, true,  true,  100, 0, 0, 0},
            {"idx_course_dept", IndexType::HASH,  "course", {"dept_name"}, false, false, 100, 0, 25, 0.2}
        };
        t.computeDerived();
        addTable(t);
    }

    //department(dept_name, building, budget)
    {
        TableStats t;
        t.name = "department";
        t.numTuples = 50;
        t.tupleSize = 80;
        t.columns = {
            {"dept_name", "string", 50,  0, 0,          true,  false, "", ""},
            {"building",  "string", 15,  0, 0,          false, false, "", ""},
            {"budget",    "float",  50,  50000, 1000000, false, false, "", ""}
        };
        t.indices = {
            {"idx_dept_name",     IndexType::BTREE,  "department", {"dept_name"}, true,  true,  100, 0, 0, 0},
            {"idx_dept_building", IndexType::BITMAP, "department", {"building"},  false, false, 100, 0, 0, 0}
        };
        t.computeDerived();
        addTable(t);
    }

    //teaches(ID, course_id, sec_id, semester, year)
    {
        TableStats t;
        t.name = "teaches";
        t.numTuples = 10000;
        t.tupleSize = 60;
        t.columns = {
            {"ID",        "int",    5000, 1, 5000, false, true, "instructor", "ID"},
            {"course_id", "string", 500,  0, 0,    false, true, "course", "course_id"},
            {"sec_id",    "string", 10,   0, 0,    false, false, "", ""},
            {"semester",  "string", 3,    0, 0,    false, false, "", ""},
            {"year",      "int",    10,   2010, 2025, false, false, "", ""}
        };
        t.indices = {
            {"idx_teaches_id",     IndexType::BTREE, "teaches", {"ID"},        false, false, 100, 0, 0, 0},
            {"idx_teaches_course", IndexType::BTREE, "teaches", {"course_id"}, false, false, 100, 0, 0, 0}
        };
        t.computeDerived();
    }
}


bool Catalog::loadFromPostgres(const string& conninfo) {
    (void)conninfo;
    return false;
}
