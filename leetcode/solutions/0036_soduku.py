class Solution:
    def isValidSudoku(self, board: List[List[str]]) -> bool:
        
        row = [set() for _ in range(9)]
        col = [set() for _ in range(9)]
        square = [set() for _ in range(9)]

        print(row)
        
        n = len(board)
        m = len(board[0])

        for r in range(9):
            for c in range(9):

                num = board[r][c]

                if num == ".":
                    continue

                if num in row[r]:
                    return False

                if num in col[c]:
                    return False

                box = (r // 3) * 3 + (c // 3)

                if num in square[box]:
                    return False

                row[r].add(num)
                col[c].add(num)
                square[box].add(num)

        return True

