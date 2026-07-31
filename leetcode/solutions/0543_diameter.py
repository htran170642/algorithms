# Definition for a binary tree node.
# class TreeNode:
#     def __init__(self, val=0, left=None, right=None):
#         self.val = val
#         self.left = left
#         self.right = right
class Solution:
    def diameterOfBinaryTree(self, root: Optional[TreeNode]) -> int:
        diameter = 0
        '''
        dfs(1)
        │
        ├── dfs(2)
        │   │
        │   ├── dfs(4)
        │   │      return 1
        │   │
        │   ├── dfs(5)
        │   │      return 1
        │   │
        │   ├── diameter = 1 + 1 = 2
        │   └── return 2
        │
        ├── dfs(3)
        │      return 1
        │
        ├── diameter = 2 + 1 = 3
        └── return 3
        '''
        def dfs(node):
            nonlocal diameter

            if node is None:
                return 0

            left = dfs(node.left)
            right = dfs(node.right)

            diameter = max(diameter, left + right)

            return 1 + max(left, right)

        dfs(root)
        return diameter