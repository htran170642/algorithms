# Definition for a binary tree node.
# class TreeNode:
#     def __init__(self, val=0, left=None, right=None):
#         self.val = val
#         self.left = left
#         self.right = right
class Solution:
    def maxDepth(self, root: Optional[TreeNode]) -> int:
        # ans  = 0

        # if not root:
        #     return ans

        # q = deque([root])

        # while q:
        #     num_nodes = len(q)

        #     for _ in range(num_nodes):
        #         node = q.popleft()

        #         if node.left:
        #             q.append(node.left)

        #         if node.right:
        #             q.append(node.right)

        #     ans += 1



        # return ans

        if root is None:
            return 0

        left = self.maxDepth(root.left)
        right = self.maxDepth(root.right)

        return 1 + max(left, right)