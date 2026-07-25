class MinStack:

    def __init__(self):
        self.stack = []

    def push(self, value: int) -> None:
        # print(self.stack)
        if not self.stack:
            self.stack.append((value, value))
        else:
            current_min = self.stack[-1][1]
            self.stack.append((value, min(current_min, value)))

    def pop(self) -> None:
        if not self.stack:
            raise
        
        self.stack.pop()

    def top(self) -> int:
        if not self.stack:
            raise
        
        return self.stack[-1][0]

    def getMin(self) -> int:
        if not self.stack:
            raise
        
        return self.stack[-1][1]
        


# Your MinStack object will be instantiated and called as such:
# obj = MinStack()
# obj.push(value)
# obj.pop()
# param_3 = obj.top()
# param_4 = obj.getMin()