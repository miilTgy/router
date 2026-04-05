#pragma once

#include <algorithm>
#include <stdexcept>

#include "common.h"

inline int CellIndex(int r, int c, int cols) {
    return r * cols + c;
}

inline int HorizontalEdgeIndex(int r, int c, int cols) {
    return r * (cols - 1) + c;
}

inline int VerticalEdgeIndex(int r, int c, int cols) {
    return r * cols + c;
}

inline bool InBounds(int r, int c, int rows, int cols) {
    return r >= 0 && r < rows && c >= 0 && c < cols;
}

inline bool HasRightEdge(int r, int c, int rows, int cols) {
    return InBounds(r, c, rows, cols) && c + 1 < cols;
}

inline bool HasLeftEdge(int r, int c, int rows, int cols) {
    return InBounds(r, c, rows, cols) && c - 1 >= 0;
}

inline bool HasUpEdge(int r, int c, int rows, int cols) {
    return InBounds(r, c, rows, cols) && r - 1 >= 0;
}

inline bool HasDownEdge(int r, int c, int rows, int cols) {
    return InBounds(r, c, rows, cols) && r + 1 < rows;
}

inline int Rows(const RoutingDB& db) {
    if (db.problem == nullptr) {
        throw std::logic_error("RoutingDB is not bound to a Problem");
    }
    return db.problem->rows;
}

inline int Cols(const RoutingDB& db) {
    if (db.problem == nullptr) {
        throw std::logic_error("RoutingDB is not bound to a Problem");
    }
    return db.problem->cols;
}

inline int HorizontalCapacity(const RoutingDB& db) {
    if (db.problem == nullptr) {
        throw std::logic_error("RoutingDB is not bound to a Problem");
    }
    return db.problem->horizontal_capacity;
}

inline int VerticalCapacity(const RoutingDB& db) {
    if (db.problem == nullptr) {
        throw std::logic_error("RoutingDB is not bound to a Problem");
    }
    return db.problem->vertical_capacity;
}

inline bool IsBlockedCell(const Problem& problem, int r, int c) {
    return problem.blocked[CellIndex(r, c, problem.cols)] != 0;
}

inline bool IsBlockedCell(const RoutingDB& db, int r, int c) {
    return IsBlockedCell(*db.problem, r, c);
}

inline bool EdgeEndpointsAvailable(const RoutingDB& db, int r0, int c0, int r1, int c1) {
    return InBounds(r0, c0, Rows(db), Cols(db)) &&
           InBounds(r1, c1, Rows(db), Cols(db)) &&
           !IsBlockedCell(db, r0, c0) &&
           !IsBlockedCell(db, r1, c1);
}

inline bool IsHorizontalEdgeAvailable(const RoutingDB& db, int r, int c) {
    return HasRightEdge(r, c, Rows(db), Cols(db)) &&
           EdgeEndpointsAvailable(db, r, c, r, c + 1);
}

inline bool IsVerticalEdgeAvailable(const RoutingDB& db, int r, int c) {
    return HasDownEdge(r, c, Rows(db), Cols(db)) &&
           EdgeEndpointsAvailable(db, r, c, r + 1, c);
}

inline bool HasEdge(const RoutingDB& db, int r, int c, Dir dir) {
    switch (dir) {
        case Dir::kUp:
            return HasUpEdge(r, c, Rows(db), Cols(db));
        case Dir::kDown:
            return HasDownEdge(r, c, Rows(db), Cols(db));
        case Dir::kLeft:
            return HasLeftEdge(r, c, Rows(db), Cols(db));
        case Dir::kRight:
            return HasRightEdge(r, c, Rows(db), Cols(db));
    }
    return false;
}

inline bool IsEdgeAvailable(const RoutingDB& db, int r, int c, Dir dir) {
    switch (dir) {
        case Dir::kUp:
            return HasUpEdge(r, c, Rows(db), Cols(db)) &&
                   EdgeEndpointsAvailable(db, r, c, r - 1, c);
        case Dir::kDown:
            return IsVerticalEdgeAvailable(db, r, c);
        case Dir::kLeft:
            return HasLeftEdge(r, c, Rows(db), Cols(db)) &&
                   EdgeEndpointsAvailable(db, r, c, r, c - 1);
        case Dir::kRight:
            return IsHorizontalEdgeAvailable(db, r, c);
    }
    return false;
}

inline Point NeighborPoint(int r, int c, Dir dir) {
    switch (dir) {
        case Dir::kUp:
            return Point{r - 1, c};
        case Dir::kDown:
            return Point{r + 1, c};
        case Dir::kLeft:
            return Point{r, c - 1};
        case Dir::kRight:
            return Point{r, c + 1};
    }
    return Point{r, c};
}

inline EdgeState& MutableEdgeState(RoutingDB& db, int r, int c, Dir dir) {
    switch (dir) {
        case Dir::kUp:
            return db.vertical_edges[VerticalEdgeIndex(r - 1, c, Cols(db))];
        case Dir::kDown:
            return db.vertical_edges[VerticalEdgeIndex(r, c, Cols(db))];
        case Dir::kLeft:
            return db.horizontal_edges[HorizontalEdgeIndex(r, c - 1, Cols(db))];
        case Dir::kRight:
            return db.horizontal_edges[HorizontalEdgeIndex(r, c, Cols(db))];
    }
    throw std::logic_error("Invalid direction");
}

inline const EdgeState& GetEdgeState(const RoutingDB& db, int r, int c, Dir dir) {
    switch (dir) {
        case Dir::kUp:
            return db.vertical_edges[VerticalEdgeIndex(r - 1, c, Cols(db))];
        case Dir::kDown:
            return db.vertical_edges[VerticalEdgeIndex(r, c, Cols(db))];
        case Dir::kLeft:
            return db.horizontal_edges[HorizontalEdgeIndex(r, c - 1, Cols(db))];
        case Dir::kRight:
            return db.horizontal_edges[HorizontalEdgeIndex(r, c, Cols(db))];
    }
    throw std::logic_error("Invalid direction");
}

inline int EdgeCapacity(const RoutingDB& db, Dir dir) {
    switch (dir) {
        case Dir::kUp:
        case Dir::kDown:
            return VerticalCapacity(db);
        case Dir::kLeft:
        case Dir::kRight:
            return HorizontalCapacity(db);
    }
    throw std::logic_error("Invalid direction");
}

inline int EdgeOverflow(const RoutingDB& db, int r, int c, Dir dir) {
    const EdgeState& edge = GetEdgeState(db, r, c, dir);
    return std::max(0, edge.usage - EdgeCapacity(db, dir));
}
