class Solution:
    def numIslands(self, grid: List[List[str]]) -> int:
        rows = len(grid)
        cols = len(grid[0])

        def dfs(r, c):
            if r < 0 or r >= rows or c < 0 or c >= cols:
                return
            if grid[r][c] == '0':
                return
            grid[r][c] = '0'

            dfs(r, c+1)
            dfs(r, c -1)
            dfs(r+1, c)
            dfs(r-1, c)

        def bfs(r, c):
            q = deque([(r, c)])
            grid[r][c] = "0"

            while q:
                r,c = q.popleft()

                directions = [(0, 1), (0, -1), (1, 0), (-1, 0)]

                for dr, dc in directions:
                    nr, nc = r + dr, c + dc

                    if 0 <= nr < rows and 0 <= nc < cols and grid[nr][nc] == '1':
                        q.append((nr, nc))
                        grid[nr][nc] = '0'

        count = 0
        for i in range(len(grid)):
            for j in range(len(grid[0])):
                if grid[i][j] == '1':
                    count += 1
                    bfs(i, j)

        return count