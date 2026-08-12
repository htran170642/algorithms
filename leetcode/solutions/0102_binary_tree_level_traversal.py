# Definition for a binary tree node.
# class TreeNode:
#     def __init__(self, val=0, left=None, right=None):
#         self.val = val
#         self.left = left
#         self.right = right
class Solution:
    def levelOrder(self, root: Optional[TreeNode]) -> List[List[int]]:
        if not root:
            return []

        q = deque([root])

        ans = []

        while q:
            num = len(q)

            node_list = []
            for _ in range(num):
                node = q.popleft()

                node_list.append(node.val)

                if node.left:
                    q.append(node.left)

                if node.right:
                    q.append(node.right)

            ans.append(node_list)

        return ans

        # node chỉ lấy ra 1 lần, O(n) time, O(n/2) space