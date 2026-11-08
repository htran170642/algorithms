```bash
    1  #q1
    2  k get pods
    3  #q2
    4  k get replicasets.apps 
    5  #q3
    6  k get replicasets.apps 
    7  #q5
    8  k describe rs new-replica-set | grep -i image
    9  #q6
   10  k get replicasets.apps 
   11  #q7
   12  k get pod
   13  k delete pod new-replica-set-cb9v9
   14  k get pod -w
   15  k get pod 
   16  #q11
   17  vim /root/replicaset-definition-1.yaml 
   18  k apply -f /root/replicaset-definition-1.yaml 
   19  kubectl api-resources | grep replicaset
   20  vim /root/replicaset-definition-1.yaml 
   21  k apply -f /root/replicaset-definition-1.yaml 
   22  k get rs
   23  #q12
   24  vim /root/replicaset-definition-2.yaml 
   25  k apply -f /root/replicaset-definition-2.yaml 
   26  k get pod
   27  #q13
   28  k delete rs replicaset-1
   29  k delete rs replicaset-2
   30  k get rs
   31  #q14
   32  k edit replicasets.apps new-replica-set 
   33  k get pods -w
   34  k delete pods --all
   35  k get pods -w
   36  #q15
   37  k scale replicaset new-replica-set --replica=5
   38  k scale replicaset new-replica-set --replicas=5
   39  k get pods -w
   40  #q16
   41  k scale replicaset new-replica-set --replicas=2
   42  k get pods -w
   43  history
```