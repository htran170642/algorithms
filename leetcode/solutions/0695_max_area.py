class Solution:
    def maxAreaOfIsland(self, grid: List[List[int]]) -> int:
        rows = len(grid)
        cols = len(grid[0])

        def bfs(r, c):
            q = deque([(r, c)])
            grid[r][c] = 0
            area = 0
            while q:
                r,c = q.popleft()
                area += 1
                directions = [(0, 1), (0, -1), (1, 0), (-1, 0)]

                for dr, dc in directions:
                    nr, nc = r + dr, c + dc

                    if 0 <= nr < rows and 0 <= nc < cols and grid[nr][nc] == 1:
                        q.append((nr, nc))
                        grid[nr][nc] = 0

            return area
        
        ans = 0
        for r in range(rows):
            for c in range(cols):
                if grid[r][c] == 1:
                    area = bfs(r,c)
                    ans = max(ans, area)

        return ans


