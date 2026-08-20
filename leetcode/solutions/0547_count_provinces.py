class Solution:
    def findCircleNum(self, isConnected: List[List[int]]) -> int:
        n = len(isConnected)

        visited = [False] * n
        provinces = 0

        for city in range(n):
            if visited[city]:
                continue

            provinces += 1

            q = deque([city])
            visited[city] = True

            while q:
                cur = q.popleft()

                for nei in range(n):
                    if isConnected[cur][nei] == 1 and not visited[nei]:
                        visited[nei]= True
                        q.append(nei)

        return provinces
